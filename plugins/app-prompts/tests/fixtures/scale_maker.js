/* Decent Cocktails - in-page scale maker (ported from support/scale/js/cocktail_maker).
   Web Bluetooth + weigh/step state machine, driven by window.MAKER (server-injected).
   Flow: pre-connect shows the Connect button -> on connect, go straight to the Tare step
   (connect row hidden) -> Tare -> guided weighing begins: every step tares the scale first, so
   the scale shows what that step added, and the drink's weight is the sum of the steps' tallies. While open, body.mkOn hides the rest
   of the recipe page and the steps list sits in a second column (CSS in lib/direct.tcl). */
(function () {
  "use strict";
  var M = window.MAKER;
  var root = document.getElementById("mk");
  if (!M || !root) return;

  // Decent Scale BLE constants
  var SVC = 0xfff0, RDCHR = 0xfff4, WRCHR = 0x36f5;
  var TARE = [0x03, 0x0f, 0x01, 0x00, 0x00, 0x01, 0x0c];
  var HEARTBEAT = [0x03, 0x0a, 0x03, 0xff, 0xff, 0x00, 0x0a];
  var ZERO_TOL = 0.2, STABLE_SPREAD = 0.4, STABLE_N = 4, TOL = 1.0;

  function $(id) { return document.getElementById(id); }
  var elConnect = $("mkConnect"), elStatus = $("mkStatus"), elWeight = $("mkWeight"),
      elProg = $("mkProg"), elGuide = $("mkGuide"), elIng = $("mkIng"),
      elTare = $("mkTare"), elPrev = $("mkPrev"), elNext = $("mkNext"),
      elList = $("mkList"), elFinal = $("mkFinal"), elIos = $("mkIos"),
      elMeter = $("mkMeter"), elBox = $("mkBox"), elConnRow = $("mkConnRow"),
      elTotal = $("mkTotal"), elPic = $("mkPic"), elMain = $("mkMain"), elPicImg = elPic && elPic.querySelector("img");

  // translation, as dt() in /js/shopping.js: non-English pages load the site-wide
  // decent_translation table (js/data/decent_translation_cached.adp, where this script's
  // phrases are listed). {name} placeholders are filled after the lookup.
  function dt(str, vars) {
    var T = window.decent_translation;
    var t = (T && T[str]) || str;
    if (vars) Object.keys(vars).forEach(function (k) { t = t.split("{" + k + "}").join(vars[k]); });
    return t;
  }

  var steps = M.steps || [];
  // the rows: the steps, with the serve row (strain into the glass) at row SV: last, or before
  // the garnishes, which go on the finished drink (M.serve, from lib/direct.tcl). Rows (idx,
  // starts, the added tallies) count the serve row; steps[] doesn't.
  var SV = (M.serve >= 0 && M.serve <= steps.length) ? M.serve : steps.length;
  function stepAt(r) { return r === SV ? null : steps[r < SV ? r : r - 1]; }
  function rowOf(i) { return i < SV ? i : i + 1; }
  var dev = null, server = null, wchar = null, rchar = null, hb = null, connected = false;
  var currentWeight = 0;
  // Every step starts from a tared scale, so the scale's own display shows what has been added
  // for that step; the drink's weight is kept here, as the sum of the steps' tallies (added).
  // zero: the reading that means "nothing added yet" in the current tare frame (~0 once the
  // scale has tared; the reading itself if the tare never lands); null while taring.
  // zeroStep: the step whose start that frame belongs to (Next tallies a step and sends the
  // next step's tare at once). totalInCup = the sum of the tallies of the steps before zeroStep.
  // rawMode: the Tare step shows the raw reading.
  var zero = null, zeroStep = -1, rawMode = true, madeMarked = false;
  var taring = false, tareAt = 0, tareTimer = null, onZero = null;
  var TARE_MIN_MS = 400, TARE_MAX_MS = 2000, TARE_ZERO = 0.5;
  // doneWeight: on the Done screen the readout holds the drink's weight (what was weighed in,
  // step by step): by then it has been poured out of the shaker, so the live reading is ~0
  var doneWeight = null;
  var starts = [];
  var idx = 0, totalInCup = 0, added = {}, stable = [], dosing = false, dset = null, doseSaved = false;

  function isCounted(s) {
    if (!s || !s.ing) return false;
    var ing = s.ing.toLowerCase(), mix = (s.mix || "").toLowerCase();
    if (ing === "cherry" && mix === "garnish") return true;
    var L = ["mint sprig", "mint leaf", "orange slice", "cucumber slice", "tangerine zest",
      "lime wheel", "lemon peel", "bitters", "absinthe", "fennel bulb", "orange twist",
      "cucumber ribbon", "orange peel", "salt", "kosher salt", "ground black pepper",
      "celery salt", "goji berry", "egg", "cocoa powder", "sugar", "cayenne",
      "whipped cream", "dill"];
    return L.some(function (it) { return ing.indexOf(it) >= 0; });
  }
  // batch size chosen on the recipe page ("Make − 1 +"); every amount scales with it
  function bn() { return window.pdtBatch || 1; }
  function scaleQ(q) { return window.pdtScaleQ ? window.pdtScaleQ(q, bn()) : q; }
  function grams(s) { return (parseFloat(s.mg) || 0) / 1000 * bn(); }
  function weighed(s) { return !isCounted(s) && !s.cnt && parseFloat(s.mg) > 0; }
  // the recipe's weight: every step with one, counted ones too (2 cucumber slices ~16 g), since
  // their weight is tallied when Next is pressed
  function target() { return steps.reduce(function (a, s, i) { return i < SV && parseFloat(s.mg) > 0 ? a + grams(s) : a; }, 0); }

  function esc(t) { return String(t == null ? "" : t).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;"); }
  function qtyfmt(q) { return String(q == null ? "" : q).replace(/([0-9])([A-Za-z])/g, "$1 $2"); } // "2dashes" -> "2 dashes"
  // msg: a string, or an array of lines shown as separate paragraphs
  function guide(msg, kind) {
    show(elGuide, true);
    if (elGuide) {
      if (Array.isArray(msg)) {
        elGuide.textContent = "";
        msg.forEach(function (line) { var p = document.createElement("span"); p.className = "mkGuideLine"; p.textContent = line; elGuide.appendChild(p); });
      } else elGuide.textContent = msg; elGuide.className = "mkGuide" + (kind ? " " + kind : "");
      elGuide.onclick = elGuide.onkeydown = null; elGuide.removeAttribute("role"); elGuide.removeAttribute("tabindex");
    }
  }
  function setIng(s) { if (elIng) { show(elIng, true); elIng.textContent = s && s.ing ? (s.ingt || s.ing) : ""; } }
  function setWeight(w) { currentWeight = w; checkTare(); showWeight(); if (dosing && zero !== null) onDose(w); }
  // what has been added in the current tare frame (0 while the scale tares)
  function amount() { return (taring || zero === null) ? 0 : currentWeight - zero; }
  function showWeight() {
    var w = currentWeight;
    if (doneWeight !== null) { if (elWeight) elWeight.textContent = doneWeight.toFixed(1); return; }
    if (rawMode) { if (elWeight) elWeight.textContent = w.toFixed(1); return; }
    var total = totalInCup + amount();   // the drink so far: the steps weighed in + this one
    // a step's readout is what was added for it; the serve row's is the whole drink
    if (elWeight) elWeight.textContent = (idx >= SV ? total : amount()).toFixed(1);
    if (elTotal) elTotal.textContent = dt("Total: {g}g", { g: total.toFixed(1) });
  }
  // tare the scale and wait for it to read ~0 (at least TARE_MIN_MS, so a reading from before
  // the tare isn't taken for it); if it never does (a write failed), the reading after
  // TARE_MAX_MS becomes the zero instead. onZero runs once the frame's zero is known.
  function tareNow() {
    taring = true; zero = null; tareAt = Date.now(); onZero = null;
    tareCmd();
    if (!tareTimer) tareTimer = setInterval(checkTare, 100);
  }
  function checkTare() {
    if (!taring) return;
    var t = Date.now() - tareAt;
    if ((t >= TARE_MIN_MS && Math.abs(currentWeight) <= TARE_ZERO) || t >= TARE_MAX_MS) {
      var f = onZero;
      stopTare(); zero = currentWeight; showWeight();
      if (f) f();
    }
  }
  function stopTare() { taring = false; onZero = null; if (tareTimer) { clearInterval(tareTimer); tareTimer = null; } }
  function whenZero(fn) { if (taring) onZero = fn; else fn(); }
  function setProg(pct, color) { if (!elProg) return; elProg.style.width = Math.max(0, Math.min(100, pct)) + "%"; elProg.className = "mkProgFill" + (color ? " " + color : ""); }
  function show(el, on) { if (el) el.style.display = on ? "" : "none"; }
  // the current ingredient's picture, in a left column of the weighing box, as tall as the
  // right column (weight, name, guide, buttons); null hides it (Tare, serve, done)
  function setPic(src) {
    if (!elPic || !elPicImg) return;
    if (!src) { elPic.hidden = true; elPicImg.removeAttribute("src"); return; }
    if (elPicImg.getAttribute("src") !== src) elPicImg.src = src;
    elPic.hidden = false; fitPic();
  }
  function fitPic() { if (elPic && !elPic.hidden && elMain) elPicImg.style.height = elMain.offsetHeight + "px"; }
  if (elMain && window.ResizeObserver) new ResizeObserver(fitPic).observe(elMain);
  else window.addEventListener("resize", fitPic);

  function renderList() {
    if (!elList) return;
    // same markup as the recipe's .spec .r rows (number, thumbnail, type/action, name, amount)
    var html = "";
    for (var r = 0; r <= steps.length; r++) {
      var st = (r < idx ? " mkdone" : "") + (r === idx ? " mkcur" : ""), s = stepAt(r);
      if (!s) {
        // the serve row: the method + the glass to serve it in (mirrors the recipe page)
        html += '<div class="r finalstep' + st + '"><span class="ix">' + (r + 1) + '</span><div class="ingwrap">' +
          (M.glassthumb || '<span class="rthumb glassthumb"></span>') +
          '<div class="rmeta"><div class="rtyp">' + esc(M.glass || dt("Glass")) + '</div><div class="mtx">' + esc(dt("Glass")) + '</div></div>' +
          '<div class="in">' + esc(M.howto || "") + '</div></div><div class="amt"></div></div>';
        continue;
      }
      var thumb = s.img
        ? '<span class="rthumb"><img src="' + s.img.replace(/\/([^\/]+)\.png$/, "/t/$1.jpg") + '" alt="" loading="lazy"></span>'
        : '<span class="rthumb"></span>';
      html += '<div class="r' + st + '"><span class="ix">' + (r + 1) + '</span><div class="ingwrap">' + thumb +
        '<div class="rmeta"><div class="rtyp">' + esc(s.typ) + '</div><div class="mtx">' + esc(s.mixt || s.mix || dt("Add")) + '</div></div>' +
        '<div class="in">' + esc(s.ingt || s.ing) + '</div></div><div class="amt">' + (window.pdtAmt ? window.pdtAmt(+s.mg, !!s.cnt, s.qtyt, bn()) : (s.amt || "")) + "</div></div>";
    }
    elList.innerHTML = html;
  }

  // --- BLE ---
  function parse(ev) {
    var d = ev.target.value;
    if (d.getUint8(0) !== 0x03 || d.byteLength !== 7) return;
    var t = d.getUint8(1);
    if (t === 0xce || t === 0xca) setWeight(d.getInt16(2, false) / 10); // big-endian int16, 0.1g units
  }
  async function connect() {
    if (!navigator.bluetooth) { guide(dt("This browser has no Web Bluetooth. Use Chrome on desktop or Android."), "error"); return; }
    try {
      if (elStatus) elStatus.textContent = dt("Scanning…");
      dev = (dev && dev.gatt && dev.gatt.connected ? dev : null) ||
        await navigator.bluetooth.requestDevice({ filters: [{ name: "Decent Scale" }], optionalServices: [SVC] });
      await attach(dev);
    } catch (e) { if (elStatus) elStatus.textContent = ""; guide(dt("Could not connect: {error}", { error: e.message || e }), "error"); }
  }
  // Decent Scales this site was granted, from getDevices(). Beacio on iOS keeps the BLE link up
  // until the tab closes, and a connected scale doesn't advertise, so after a reload requestDevice
  // can't find it; but getDevices() still lists it and gatt.connect() on it works (verified with
  // /cocktails/bletest.html). Its gatt.connected reads false in the new page, so don't trust it.
  // The name can be missing on a fresh proxy object, so accept a nameless device too.
  async function grantedScales() {
    if (!navigator.bluetooth || !navigator.bluetooth.getDevices) return [];
    try {
      var list = await navigator.bluetooth.getDevices();
      return list.filter(function (d) { return d && d.gatt && (!d.name || d.name === "Decent Scale"); });
    } catch (e) { return []; }
  }
  function withTimeout(p, ms) {
    return Promise.race([p, new Promise(function (_, rej) { setTimeout(function () { rej(new Error("timeout")); }, ms); })]);
  }
  async function attach(d) {
    dev = d;
    server = await dev.gatt.connect();   // resolves at once when already connected
    var svc = await server.getPrimaryService(SVC);
    rchar = await svc.getCharacteristic(RDCHR);
    wchar = await svc.getCharacteristic(WRCHR);
    await rchar.startNotifications();
    rchar.addEventListener("characteristicvaluechanged", parse);
    dev.addEventListener("gattserverdisconnected", onDisc);
    connected = true;
    if (elConnect) elConnect.textContent = dt("Disconnect");
    startHB();
    startMaking();            // as soon as connected, go straight to the Tare step
  }
  // on opening the maker, reuse the scale from earlier on this page or a previous page in this tab,
  // skipping the Connect step. A granted scale that is off or out of range never answers
  // gatt.connect() (no timeout of its own), so cap each try and fall back to the Connect button.
  var autoTrying = false;
  async function autoConnect() {
    if (connected) { startMaking(); return; }
    if (autoTrying) return;
    autoTrying = true;
    try {
      var list = await grantedScales();
      for (var i = 0; i < list.length && !connected; i++) {
        if (elStatus) elStatus.textContent = dt("Reconnecting…");
        try { await withTimeout(attach(list[i]), 6000); }
        catch (e) { try { list[i].gatt.disconnect(); } catch (e2) { } }
      }
    } finally {
      autoTrying = false;
      if (!connected && elStatus) elStatus.textContent = "";
    }
  }
  function startHB() { stopHB(); send(HEARTBEAT); hb = setInterval(function () { send(HEARTBEAT).catch(function () { disconnect(); }); }, 4000); }
  function stopHB() { if (hb) { clearInterval(hb); hb = null; } }
  async function send(bytes) { if (!wchar) throw new Error("not connected"); await wchar.writeValue(new Uint8Array(bytes)); }
  async function tareCmd() { try { await send(TARE); } catch (e) { } }
  function onDisc() { connected = false; stopHB(); if (elConnect) elConnect.textContent = dt("Connect to scale"); if (elStatus) elStatus.textContent = ""; viewPreconnect(); }
  async function disconnect() { stopHB(); try { if (server && server.connected) server.disconnect(); } catch (e) { } connected = false; if (elConnect) elConnect.textContent = dt("Connect to scale"); if (elStatus) elStatus.textContent = ""; viewPreconnect(); }

  // --- dosing / weigh state machine ---
  function startDose(target) { dosing = true; dset = { t: target, lo: target - TOL, hi: target + TOL }; stable = []; doseSaved = false; setProg(0, ""); }
  function stopDose() { dosing = false; setProg(0, ""); }
  function isStable() { if (stable.length < STABLE_N) return false; var r = stable.slice(-STABLE_N); return (Math.max.apply(null, r) - Math.min.apply(null, r)) <= STABLE_SPREAD; }
  // w is the raw reading; the dose is what was added since this step's tare (w - zero)
  function onDose(w) {
    var a = w - zero, t = dset.t, lo = dset.lo, hi = dset.hi, rem = t - a;
    setProg(t > 0 ? a / t * 100 : 100, (a >= lo && a <= hi) ? "ok" : (a < lo ? "under" : "over"));
    stable.push(w); if (stable.length > STABLE_N) stable.shift();
    if (isStable()) {
      if (doseSaved) return;
      if (a >= lo && a <= hi) { doseSaved = true; stepDone(); }
      else if (a > hi) guide(dt("Over by {g}g", { g: (a - t).toFixed(1) }), "error");
      else if (a > ZERO_TOL) guide(dt("Add {g}g", { g: rem.toFixed(1) }), "info");
    } else {
      doseSaved = false;
      if (rem > 0) guide(dt("Add {g}g", { g: rem.toFixed(1) }), "info");
      else guide(dt("Pouring… over by {g}g", { g: (-rem).toFixed(1) }), "warn");
    }
  }
  // tally what this step added, add it to the drink, and tare for the next step
  function tally() {
    var delta = amount();
    added["step_" + (idx + 1)] = delta; totalInCup += delta;
    tareNow(); zeroStep = idx + 1;
    return delta;
  }
  // the dose is in range: say so, but never move on by itself; the user always presses Next
  // (manualNext tallies it). Dosing stays live, so pouring more still shows "Over by".
  function stepDone() {
    guide(dt("Good! Press Next for the next ingredient."), "success");
  }

  // --- step flow (steps become visible only after Tare) ---
  // z (from Prev): keep the scale as it is and use z as this step's zero, instead of taring
  function showStep(i, z) {
    idx = i; renderList(); doneWeight = null; rawMode = false;
    if (i <= steps.length) starts[i] = totalInCup;   // the drink's weight when this step began (for Prev)
    if (i > steps.length) { complete(); return; }   // past the last row -> accuracy tally
    stopDose();
    if (z !== undefined) { stopTare(); zero = z; zeroStep = i; }
    else if (zeroStep !== i) { tareNow(); zeroStep = i; }   // every step starts from a tared scale
    if (i === SV) {                                  // the serve row: method + Prev/Next
      // reaching the glass counts as making it (logged-in): not everyone clicks through the
      // garnishes to Done. Once per run of the maker; pdtMarks.add also stamps the day.
      if (!madeMarked && window.pdtMarks && M.id) { madeMarked = true; window.pdtMarks.add("made", M.id); }
      show(elIng, false); show(elTare, false); setPic(M.glassimg || null);
      showWeight(); show(elTotal, true);
      show(elPrev, true); show(elNext, true);
      if (elFinal) elFinal.hidden = true;
      guide(M.howto || dt("Stir or shake, then serve"), "success");
      return;
    }
    var s = stepAt(i);
    showWeight(); show(elTotal, true);
    setIng(s); setPic(s.big || s.img); show(elPrev, true); show(elNext, true);
    if (i < SV && weighed(s)) {   // after the serve row the drink is in the glass: nothing is weighed
      guide(dt("Add {g}g", { g: grams(s).toFixed(1) }), "info");
      whenZero(function () { if (idx === i) startDose(grams(s)); });
    } else {
      guide(dt("{action}, then press Next.", { action: (s.mixt || s.mix || dt("Add")) + (s.qty ? " " + scaleQ(s.qtyt || qtyfmt(s.qty)) : "") }), "info");
    }
  }
  // Next by hand: a counted step (2 cucumber slices, an egg white) or a weighed one cut short is
  // tallied at whatever it added, then the next step tares, so its weight never lands in the next
  // ingredient's dose.
  function manualNext() {
    // the serve row and the garnishes after it add nothing to the tally (the drink has left the scale)
    if (idx >= SV) { stopDose(); idx++; showStep(idx); return; }
    stopDose();
    if (zeroStep <= idx) tally();
    idx++; showStep(idx);
  }
  // back one step: its readout shows what was poured for it (plus anything poured since) and its
  // target is right again, without taring (the scale still holds it): its zero is moved back by
  // the tallies since it began. The later steps' tallies are dropped, to be weighed in again.
  function prev() {
    if (idx <= 0) { startMaking(); return; }   // from the first ingredient, go back to the shaker/tare step
    var k = idx - 1, since = 0;
    for (var j = k; j < zeroStep; j++) since += added["step_" + (j + 1)] || 0;
    var z = (taring || zero === null ? 0 : zero) - since;   // mid-tare: the scale is about to read 0
    if (starts[k] != null) totalInCup = starts[k];
    for (var m = k + 1; m <= steps.length + 1; m++) delete added["step_" + m];
    showStep(k, z);
  }
  // the facts panel's numbers (admin/pdt_facts.py's model: ice dilution by Dave Arnold's
  // formulas, toppers after) for amounts f(i) = how many drinks' worth of step i went in
  function drinkFacts(f) {
    var bv = 0, be = 0, tv = 0, te = 0, sg = 0, ac = 0;
    steps.forEach(function (s, i) {
      var x = s.fx; if (!x || x.ph === "none") return;
      var k = f(i), v = x.v * k, e = v * x.abv / 100, amt = (x.per === "100g" ? x.g : x.v) * k;
      sg += amt * x.s / 100; ac += amt * x.a / 100;
      if (x.ph === "top") { tv += v; te += e; } else { bv += v; be += e; }
    });
    if (bv + tv <= 0) return null;
    var a = bv ? be / bv : 0, w = 0;
    if (M.dil === "shake") w = bv * (1.567 * a * a + 1.742 * a + 0.203);
    else if (M.dil === "stir") w = bv * (-1.21 * a * a + 1.246 * a + 0.145);
    var vol = bv + w + tv, eth = be + te, sp = 100 * sg / vol, ap = 100 * ac / vol, bal;
    if (ap >= 0.4) { var r = sp / ap; bal = r < 6 ? "Tart" : (r < 12 ? "Balanced" : "Sweet"); }
    else bal = sp < 2 ? "Dry" : (sp < 5 ? "Off-dry" : (sp < 10 ? "Sweet" : "Very sweet"));
    return { vol: vol, abv: 100 * eth / vol, eth: eth, ethg: eth * 0.789, vod: eth / 0.4, sug: sg, sugp: sp, acid: ap, bal: bal };
  }
  // drinks' worth of step i actually used: weighed steps by what the scale saw, the rest as
  // the recipe has them (at the batch size)
  function usedFactor(i) {
    var s = steps[i], per = (parseFloat(s.mg) || 0) / 1000;
    return i < SV && weighed(s) && per > 0 ? (added["step_" + (rowOf(i) + 1)] || 0) / per : bn();
  }
  // the facts rows, worded as the facts panel (lib/direct.tcl) words them
  function factsHTML(F) {
    var L = M.fl || {};
    function t(k, d) { return L[k] || d; }
    function num(v) { return String(Math.round(v * 100) / 100); }
    function row(k, lines) { return '<div class="row"><span>' + esc(k) + "</span><b>" + lines.map(esc).join("<br>") + "</b></div>"; }
    var sh = Math.round(F.vod / 30 * 2) / 2, cubes = Math.round(F.sug / 4 * 2) / 2;
    var sugg = F.sug < 10 ? String(Math.round(F.sug * 10) / 10) : String(Math.round(F.sug));
    var h = '<div class="mkFactsHead">' + esc(t("head", "As you made it")) + "</div>";
    h += row(t("vol", "Volume"), [Math.round(F.vol) + " ml"]);
    h += row(t("abv", "Strength"), [t("abvn", "{n}% ABV").replace("{n}", Math.round(F.abv))]);
    h += row(t("eth", "Alcohol"), [Math.round(F.eth) + " ml (" + Math.round(F.ethg) + " g)",
      t("vodml", "{n} ml of vodka (40%)").replace("{n}", Math.round(F.vod)),
      sh === 1 ? t("shot1", "1 shot (of 30ml)") : t("shotn", "{n} shots (of 30ml)").replace("{n}", num(sh))]);
    var sl = [sugg + " g (" + F.sugp.toFixed(1) + "%)"];
    if (cubes > 0) sl.push(cubes === 1 ? t("cube1", "1 sugar cube") : t("cuben", "{n} sugar cubes").replace("{n}", num(cubes)));
    h += row(t("sug", "Sugar"), sl);
    h += row(t("acid", "Acidity"), [F.acid < 0.005 ? t("none", "none") : F.acid.toFixed(2) + "%"]);
    h += row(F.acid >= 0.4 ? t("sour", "Sweet vs sour") : t("sweet", "Sweetness"), [t(F.bal, F.bal)]);
    return h;
  }
  window.mkFacts = function (planned) { return drinkFacts(planned ? function () { return 1; } : usedFactor); };  // testing
  function complete() {
    stopDose(); show(elNext, false); show(elTare, false); show(elPrev, false); show(elIng, false); setPic(M.photo || null);
    guide(dt("Done!"), "success");
    // the Done! panel is a button back to the recipe (the scale stays connected)
    if (elGuide) {
      elGuide.classList.add("mkDoneBtn"); elGuide.setAttribute("role", "button"); elGuide.tabIndex = 0;
      elGuide.onclick = closeMaker;
      elGuide.onkeydown = function (e) { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); closeMaker(); } };
    }
    var ut = 0; for (var k in added) ut += added[k] || 0;
    doneWeight = ut; showWeight(); show(elTotal, false); setProg(0, "");
    if (elFinal) {
      var tg = target(), diff = ut - tg;
      elFinal.hidden = false;
      elFinal.innerHTML =
        '<div class="row"><span>' + esc(dt("Recipe target")) + '</span><b>' + tg.toFixed(1) + " g</b></div>" +
        '<div class="row"><span>' + esc(dt("Your final weight")) + '</span><b>' + ut.toFixed(1) + " g</b></div>" +
        '<div class="row"><span>' + esc(dt("Difference")) + '</span><b class="' + (Math.abs(diff) <= 2 ? "ok" : "over") + '">' + (diff >= 0 ? "+" : "") + diff.toFixed(1) + " g" +
        (tg > 0 ? " (" + (diff >= 0 ? "+" : "") + (diff / tg * 100).toFixed(1) + "%)" : "") + "</b></div>";
      var F = drinkFacts(usedFactor);
      if (F) elFinal.innerHTML += factsHTML(F);
    }
  }

  // --- views ---
  function reset() { madeMarked = false; starts = []; idx = 0; totalInCup = 0; added = {}; currentWeight = 0; stable = []; stopDose(); if (elWeight) elWeight.textContent = "0.0"; if (elFinal) elFinal.hidden = true; setPic(null); stopTare(); zero = null; zeroStep = -1; rawMode = true; doneWeight = null; show(elTotal, false); }
  function viewPreconnect() {
    // the Connect button (and the steps, in the second column) until a scale is connected
    reset();
    show(elConnRow, true); show(elBox, false); show(elList, true); renderList();
    show(elIng, false); show(elTare, false); show(elPrev, false); show(elNext, false);
    if (elFinal) elFinal.hidden = true;
  }
  function startMaking() {
    // connected: hide the connect row, show the bordered stage with the Tare prompt
    if (!connected) return;
    reset();
    show(elConnRow, false); show(elBox, true); show(elList, true); renderList();
    show(elIng, false); show(elTare, true); show(elPrev, false); show(elNext, false);
    if (elFinal) elFinal.hidden = true;
    guide(dt(M.dil ? "Put ice in your cocktail shaker and place it on the scale." : "Place your cocktail shaker on the scale."), "info");
    setPic(M.tareimg || null);   // a shaker tin on the Half Decent Scale
  }
  // the shaker (with its ice) is on the scale: start the first step, which tares it
  function doTare() {
    reset();
    show(elTare, false);
    showStep(0);
  }


  // gate on the Web Bluetooth API itself, not the browser: on iOS any browser works once
  // the Beacio app (which provides Web Bluetooth) is installed; elsewhere it needs Chrome
  var BEACIO = "https://apps.apple.com/gb/app/beacio/id6761301368";
  function iosGate() {
    if (navigator.bluetooth) {
      if (elIos) elIos.hidden = true;
      if (elConnect) elConnect.hidden = false;
      return false;
    }
    var ua = navigator.userAgent || "";
    var isIOS = /iPad|iPhone|iPod/.test(ua) || (navigator.platform === "MacIntel" && navigator.maxTouchPoints > 1);
    if (isIOS) {
      if (elConnect) elConnect.hidden = true;
      if (elIos) {
        elIos.innerHTML = "";
        var p = document.createElement("p");
        p.style.margin = "0 0 12px";
        // Beacio is a Safari extension, and only Safari runs extensions on iOS. Always say so:
        // the UA can't tell Safari apart (Brave on iOS sends Safari's exact UA)
        p.textContent = dt("To use the scale on iPhone or iPad, open this page in Safari and install the free Beacio app, which adds Bluetooth to Safari. You can still Preview the steps.");
        var a = document.createElement("a");
        a.className = "mkBtn primary"; a.href = BEACIO; a.target = "_blank"; a.rel = "noopener";
        a.style.display = "inline-block"; a.style.textDecoration = "none";
        a.textContent = dt("Install Beacio (Safari)");
        elIos.appendChild(p); elIos.appendChild(a);
        elIos.hidden = false;
      }
    } else {
      if (elIos) elIos.hidden = false;
      if (elConnect) { elConnect.disabled = true; elConnect.textContent = dt("Requires Chrome"); }
    }
    return true;
  }

  // --- wire ---
  if (elConnect) elConnect.addEventListener("click", function () { connected ? disconnect() : connect(); });
  if (elTare) elTare.addEventListener("click", doTare);
  if (elNext) elNext.addEventListener("click", manualNext);
  if (elPrev) elPrev.addEventListener("click", prev);
  var back = $("mkBack");
  // keep the scale connected (heartbeat running) so reopening the maker goes straight to Tare
  function closeMaker() { root.hidden = true; document.body.classList.remove("mkOn"); var ing = document.getElementById("recipeIng"); if (ing) ing.hidden = false; reset(); }
  if (back) back.addEventListener("click", closeMaker);

  window.openMaker = function () {
    var ing = document.getElementById("recipeIng"); if (ing) ing.hidden = true;  // Ingredients block is replaced by the maker
    root.hidden = false;
    document.body.classList.add("mkOn");     // hides the rest of the recipe page
    var gated = iosGate();
    viewPreconnect();
    renderList();
    if (!gated) autoConnect();
    // the maker takes the steps' place at the top of the page: go to the top (the button that opens
    // it sits lower down, under the facts)
    window.scrollTo({ top: 0, behavior: "smooth" });
  };

  // dev/testing without a scale: window.mkSim(grams) feeds a fake reading
  window.mkSim = function (w) { setWeight(w); };
  renderList();
})();
