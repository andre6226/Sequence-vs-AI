</main>
<svg style="position: absolute; width: 0; height: 0; visibility: hidden; pointer-events: none;">
  <filter id="displacementFilter">
    <feTurbulence
      type="turbulence"
      baseFrequency="0.010"
      numOctaves="2" result="turbulence"
    />
    <feDisplacementMap
      in="SourceGraphic"
      in2="turbulence"
      scale="50" xChannelSelector="R"
      yChannelSelector="G"
    />
  </filter>
</svg>
<footer>
  <div class="panel">
          <div>
              <p>&copy; <?php echo date("Y"); ?> Sequence vs AI — Realizzato da Vaccari Andrea.</p>
          </div>
  </div>
</footer>
<script type="module" src="js/core/main.js"></script>
</body>
</html>