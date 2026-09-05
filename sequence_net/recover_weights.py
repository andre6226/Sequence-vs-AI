"""
Ricostruisce un checkpoint PyTorch a partire da un .onnx esportato.
Serve quando il .pth e' andato perso: i pesi sono dentro l'ONNX.

Nota: l'export ha FUSO i BatchNorm dentro le convoluzioni (i nodi Conv hanno
un bias e non ci sono nodi BatchNormalization). Il modello ricostruito e'
quindi senza BN, con le conv dotate di bias: in inferenza e' identico
all'originale, ed e' la forma giusta da cui riprendere l'addestramento.
"""
import sys, numpy as np, torch, torch.nn as nn, torch.nn.functional as F, onnx
from onnx import numpy_helper

class ResBlockFolded(nn.Module):
    def __init__(self, ch):
        super().__init__()
        self.conv1 = nn.Conv2d(ch, ch, 3, padding=1, bias=True)
        self.conv2 = nn.Conv2d(ch, ch, 3, padding=1, bias=True)
    def forward(self, x):
        out = F.relu(self.conv1(x))
        out = self.conv2(out)
        out = out + x
        return F.relu(out)

class SequenceNetFolded(nn.Module):
    """SequenceNetV2 con i BatchNorm gia' incorporati nelle convoluzioni."""
    def __init__(self, num_res_blocks=4, channels=64):
        super().__init__()
        self.conv_in = nn.Conv2d(3, channels, 3, padding=1, bias=True)
        self.res_blocks = nn.ModuleList([ResBlockFolded(channels) for _ in range(num_res_blocks)])
        self.policy_conv = nn.Conv2d(channels, 2, 1, bias=True)
        self.policy_fc = nn.Linear(200, 200)
        self.value_conv = nn.Conv2d(channels, 1, 1, bias=True)
        self.value_fc1 = nn.Linear(100 + 52, 128)
        self.value_fc2 = nn.Linear(128, 1)
    def forward(self, board_tensor, hand_tensor):
        x = F.relu(self.conv_in(board_tensor))
        for b in self.res_blocks:
            x = b(x)
        p = F.relu(self.policy_conv(x)).view(x.size(0), -1)
        policy = self.policy_fc(p)
        v = F.relu(self.value_conv(x)).view(x.size(0), -1)
        v = torch.cat([v, hand_tensor], dim=1)
        v = F.relu(self.value_fc1(v))
        value = torch.tanh(self.value_fc2(v))
        return policy, value

def recover(onnx_path, pth_path):
    init = {i.name: numpy_helper.to_array(i) for i in onnx.load(onnx_path).graph.initializer}
    model = SequenceNetFolded()
    sd = {}
    for key in model.state_dict():
        if key.endswith(".bias") and key.replace(".bias", ".weight_bias") in init:
            src = init[key.replace(".bias", ".weight_bias")]      # bias delle conv fuse
        elif key in init:
            src = init[key]                                        # pesi diretti
        else:
            raise KeyError(f"peso mancante nell'ONNX: {key}")
        sd[key] = torch.from_numpy(np.array(src, dtype=np.float32))
    model.load_state_dict(sd)
    torch.save(model.state_dict(), pth_path)
    return model

if __name__ == "__main__":
    onnx_path, pth_path = sys.argv[1], sys.argv[2]
    model = recover(onnx_path, pth_path).eval()

    # Verifica: le uscite PyTorch devono coincidere con quelle di ONNX Runtime
    import onnxruntime as ort
    sess = ort.InferenceSession(onnx_path, providers=['CPUExecutionProvider'])
    rng = np.random.default_rng(0)
    dp = dv = 0.0
    for _ in range(50):
        b = (rng.random((1,3,10,10)) < 0.25).astype(np.float32)
        h = rng.integers(0,3,(1,52)).astype(np.float32)
        po, vo = sess.run(None, {'board_input': b, 'hand_input': h})
        with torch.no_grad():
            pt, vt = model(torch.from_numpy(b), torch.from_numpy(h))
        dp = max(dp, float(np.abs(po - pt.numpy()).max()))
        dv = max(dv, float(np.abs(vo - vt.numpy()).max()))
    print(f"  salvato: {pth_path}")
    print(f"  differenza massima su 50 input casuali -> policy {dp:.2e}   value {dv:.2e}")
    print("  " + ("OK: i pesi coincidono" if max(dp,dv) < 1e-4 else "ATTENZIONE: scostamento troppo grande"))
