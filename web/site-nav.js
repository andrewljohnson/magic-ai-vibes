// Shared top-bar navigation for the telemetry site. Each page includes
// <script src="site-nav.js"></script> right after <body>; the bar keys
// the active tab off the current pathname.
(function () {
  const pages = [
    ["monitor.html", "Monitor"],
    ["matchup-matrix.html", "Bot comparison"],
    ["decks.html", "Decks"],
    ["replay.html", "Replays"],
  ];
  const here = location.pathname.split("/").pop() || "monitor.html";
  const nav = document.createElement("nav");
  nav.setAttribute("aria-label", "Site");
  nav.style.cssText =
    "display:flex;gap:4px;align-items:center;margin:0 0 18px;" +
    "padding:6px 8px;border:1px solid var(--grid,#e7e6e2);" +
    "border-radius:10px;max-width:980px;flex-wrap:wrap";
  for (const [href, label] of pages) {
    const link = document.createElement("a");
    link.href = href;
    link.textContent = label;
    const active = here === href;
    link.style.cssText =
      "padding:5px 12px;border-radius:7px;text-decoration:none;" +
      "font-size:13px;color:inherit" +
      (active
        ? ";background:var(--grid,#e7e6e2);font-weight:650"
        : ";opacity:.75");
    nav.appendChild(link);
  }
  const main = document.querySelector("main") || document.body;
  main.insertBefore(nav, main.firstChild);
})();
