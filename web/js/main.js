/* Lexicon website — shared behaviour: theme toggle, mobile nav, lightbox, code copy */

(function () {
  "use strict";

  /* ----- Theme toggle (the <html data-theme> attribute is set early in <head>) ----- */

  var themeToggle = document.getElementById("themeToggle");

  function currentTheme() {
    return document.documentElement.getAttribute("data-theme") === "dark" ? "dark" : "light";
  }

  function renderThemeIcon() {
    if (themeToggle) {
      themeToggle.textContent = currentTheme() === "dark" ? "☀️" : "🌙";
      themeToggle.setAttribute(
        "aria-label",
        currentTheme() === "dark" ? "Switch to light mode" : "Switch to dark mode"
      );
    }
  }

  if (themeToggle) {
    themeToggle.addEventListener("click", function () {
      var next = currentTheme() === "dark" ? "light" : "dark";
      document.documentElement.setAttribute("data-theme", next);
      try {
        localStorage.setItem("lexicon-theme", next);
      } catch (e) {
        /* private mode — ignore */
      }
      renderThemeIcon();
    });
    renderThemeIcon();
  }

  /* ----- Mobile navigation ----- */

  var burger = document.getElementById("navBurger");
  var nav = document.getElementById("siteNav");

  if (burger && nav) {
    burger.addEventListener("click", function () {
      var open = nav.classList.toggle("open");
      burger.setAttribute("aria-expanded", open ? "true" : "false");
    });
  }

  /* ----- Lightbox for screenshots ----- */

  var zoomables = document.querySelectorAll("figure.screenshot img, .gallery img, [data-zoom]");

  if (zoomables.length > 0) {
    var lightbox = document.createElement("div");
    lightbox.className = "lightbox";
    lightbox.innerHTML = '<img alt=""><div class="lightbox-caption"></div>';
    document.body.appendChild(lightbox);

    var lightboxImg = lightbox.querySelector("img");
    var lightboxCaption = lightbox.querySelector(".lightbox-caption");

    function closeLightbox() {
      lightbox.classList.remove("open");
      lightboxImg.src = "";
    }

    zoomables.forEach(function (img) {
      img.addEventListener("click", function () {
        lightboxImg.src = img.src;
        lightboxImg.alt = img.alt || "";
        lightboxCaption.textContent = img.alt || "";
        lightbox.classList.add("open");
      });
    });

    lightbox.addEventListener("click", closeLightbox);
    document.addEventListener("keydown", function (e) {
      if (e.key === "Escape") closeLightbox();
    });
  }

  /* ----- Copy buttons on code blocks ----- */

  document.querySelectorAll("pre").forEach(function (pre) {
    var btn = document.createElement("button");
    btn.className = "copy-btn";
    btn.type = "button";
    btn.textContent = "Copy";
    btn.addEventListener("click", function () {
      var code = pre.querySelector("code");
      var text = code ? code.innerText : pre.innerText;
      navigator.clipboard.writeText(text).then(function () {
        btn.textContent = "Copied!";
        setTimeout(function () {
          btn.textContent = "Copy";
        }, 1600);
      });
    });
    pre.appendChild(btn);
  });
})();
