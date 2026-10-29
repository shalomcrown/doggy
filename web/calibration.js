// Shared by the dog and rover pages. The setup dialog may cover the page.
// The capture panel is mounted beside the video and does not cover it.

// ================================================================================

function pagePoints(name, landscape) {
  const sizes = {
    a4: [595.28, 841.89],
    letter: [612, 792],
    a3: [841.89, 1190.55]
  };
  const pair = sizes[name] || sizes.a4;
  if (landscape) {
    return { w: pair[1], h: pair[0] };
  }
  return { w: pair[0], h: pair[1] };
}

// ================================================================================

function fitSquares(page, squareMm) {
  const margin = 12 * 72 / 25.4;
  const caption = 24;
  const square = squareMm * 72 / 25.4;
  if (square <= 1) {
    return null;
  }
  const cols = Math.floor((page.w - 2 * margin) / square);
  const rows = Math.floor((page.h - 2 * margin - caption) / square);
  if (cols < 5 || rows < 5) {
    return null;
  }
  return { cols: cols, rows: rows, margin: margin, square: square };
}

// ================================================================================

function paperLabel(name, landscape) {
  const names = { a4: "A4", letter: "Letter", a3: "A3" };
  const paper = names[name] || "A4";
  if (landscape) {
    return paper + " landscape";
  }
  return paper + " portrait";
}

// ================================================================================

function formatMm(value) {
  const rounded = Math.round(value * 10) / 10;
  if (Math.abs(rounded - Math.round(rounded)) < 0.05) {
    return String(Math.round(rounded));
  }
  return rounded.toFixed(1);
}

// ================================================================================

function pdfLiteral(text) {
  return "(" + text.replace(/\\/g, "\\\\").replace(/\(/g, "\\(").replace(/\)/g, "\\)") + ")";
}

// ================================================================================

function captionLines(layout, details) {
  const square = formatMm(details.squareMm);
  const width = formatMm(layout.cols * details.squareMm);
  const height = formatMm(layout.rows * details.squareMm);
  return [
    paperLabel(details.paper, details.landscape)
      + ". Print at 100% scale. Do not fit to page.",
    layout.cols + " x " + layout.rows + " squares, " + square + " mm each. Board "
      + width + " x " + height + " mm. Inner corners "
      + (layout.cols - 1) + " x " + (layout.rows - 1) + "."
  ];
}

// ================================================================================

function buildPdf(page, layout, details) {
  const lines = ["0 0 0 rg"];
  for (let row = 0; row < layout.rows; row += 1) {
    for (let col = 0; col < layout.cols; col += 1) {
      if (((col + row) % 2) != 0) {
        continue;
      }
      const x = layout.margin + col * layout.square;
      const y = page.h - layout.margin - (row + 1) * layout.square;
      lines.push(
        x.toFixed(2) + " " + y.toFixed(2) + " "
        + layout.square.toFixed(2) + " " + layout.square.toFixed(2) + " re f"
      );
    }
  }
  const notes = captionLines(layout, details);
  const boardBottom = page.h - layout.margin - layout.rows * layout.square;
  lines.push("BT");
  lines.push("/F1 9 Tf");
  for (let index = 0; index < notes.length; index += 1) {
    const y = boardBottom - 14 - index * 12;
    lines.push(
      "1 0 0 1 " + layout.margin.toFixed(2) + " " + y.toFixed(2) + " Tm"
    );
    lines.push(pdfLiteral(notes[index]) + " Tj");
  }
  lines.push("ET");
  const stream = lines.join("\n");
  const objects = [
    "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n",
    "2 0 obj\n<< /Type /Pages /Count 1 /Kids [3 0 R] >>\nendobj\n",
    "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "
      + page.w.toFixed(2) + " " + page.h.toFixed(2)
      + "] /Resources << /Font << /F1 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> >> >>"
      + " /Contents 4 0 R >>\nendobj\n",
    "4 0 obj\n<< /Length " + stream.length + " >>\nstream\n" + stream
      + "\nendstream\nendobj\n"
  ];
  let body = "%PDF-1.4\n";
  const offsets = [0];
  for (let index = 0; index < objects.length; index += 1) {
    offsets.push(body.length);
    body += objects[index];
  }
  const xrefAt = body.length;
  body += "xref\n0 " + (objects.length + 1) + "\n";
  body += "0000000000 65535 f \n";
  for (let index = 1; index < offsets.length; index += 1) {
    const digits = String(offsets[index]).padStart(10, "0");
    body += digits + " 00000 n \n";
  }
  body += "trailer\n<< /Size " + (objects.length + 1) + " /Root 1 0 R >>\n";
  body += "startxref\n" + xrefAt + "\n%%EOF\n";
  return body;
}

// ================================================================================

function downloadBoard(page, layout, details) {
  const pdf = buildPdf(page, layout, details);
  const blob = new Blob([pdf], { type: "application/pdf" });
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = "doggy-chessboard.pdf";
  document.body.appendChild(link);
  link.click();
  link.remove();
  URL.revokeObjectURL(url);
}

// ================================================================================

function fieldNumber(input) {
  const value = Number(input.value);
  if (Number.isFinite(value) == false) {
    return null;
  }
  return value;
}

// ================================================================================

function pictureRect(video) {
  const boxWidth = video.clientWidth;
  const boxHeight = video.clientHeight;
  const frameWidth = video.videoWidth;
  const frameHeight = video.videoHeight;
  if (frameWidth <= 0 || frameHeight <= 0 || boxWidth <= 0 || boxHeight <= 0) {
    return { x: 0, y: 0, w: boxWidth, h: boxHeight };
  }
  const scale = Math.min(boxWidth / frameWidth, boxHeight / frameHeight);
  const width = frameWidth * scale;
  const height = frameHeight * scale;
  return {
    x: (boxWidth - width) / 2,
    y: (boxHeight - height) / 2,
    w: width,
    h: height
  };
}

// ================================================================================

function placeCanvas(video, canvas) {
  const parent = video.parentElement;
  if (parent == null) {
    return;
  }
  if (canvas.parentElement !== parent) {
    parent.appendChild(canvas);
  }
  const parentBox = parent.getBoundingClientRect();
  const videoBox = video.getBoundingClientRect();
  canvas.style.left = (videoBox.left - parentBox.left) + "px";
  canvas.style.top = (videoBox.top - parentBox.top) + "px";
  canvas.style.width = videoBox.width + "px";
  canvas.style.height = videoBox.height + "px";
  canvas.width = Math.max(1, Math.round(videoBox.width));
  canvas.height = Math.max(1, Math.round(videoBox.height));
}

// ================================================================================

function strokeBox(ctx, picture, box, color, dashed, half) {
  if (box == null) {
    return;
  }
  let originX = picture.x;
  let span = picture.w;
  if (half === 0 || half === 1) {
    span = picture.w / 2;
    originX = picture.x + half * span;
  }
  const x = originX + box.x * span;
  const y = picture.y + box.y * picture.h;
  const w = box.width * span;
  const h = box.height * picture.h;
  ctx.save();
  ctx.strokeStyle = color;
  ctx.lineWidth = 2;
  ctx.setLineDash(dashed ? [8, 6] : []);
  ctx.strokeRect(x, y, w, h);
  ctx.restore();
}

// ================================================================================

function drawOverlay(video, canvas, body) {
  placeCanvas(video, canvas);
  const ctx = canvas.getContext("2d");
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  if (body == null) {
    return;
  }
  const picture = pictureRect(video);
  const stereo = body.stereo === true;
  if (body.left && body.left.found && body.left.bounds) {
    strokeBox(ctx, picture, body.left.bounds, "#3ddc97", false, stereo ? 0 : null);
  }
  if (stereo && body.right && body.right.found && body.right.bounds) {
    strokeBox(ctx, picture, body.right.bounds, "#3ddc97", false, 1);
  }
  if (body.next_target) {
    if (stereo) {
      strokeBox(ctx, picture, body.next_target, "#ffd166", true, 0);
      strokeBox(ctx, picture, body.next_target, "#ffd166", true, 1);
    } else {
      strokeBox(ctx, picture, body.next_target, "#ffd166", true, null);
    }
  }
}

// ================================================================================

function bindCalibration() {
  const button = document.getElementById("camera-calibrate");
  const mount = document.getElementById("calibration-mount");
  const video = document.getElementById("camera-video");
  if (button == null || mount == null || video == null) {
    return;
  }

  const canvas = document.createElement("canvas");
  canvas.className = "calibration-overlay";
  canvas.setAttribute("aria-hidden", "true");

  const panel = document.createElement("section");
  panel.id = "calibration-panel";
  panel.className = "calibration-panel";
  panel.hidden = true;
  panel.setAttribute("aria-label", "Camera calibration");
  panel.innerHTML = ""
    + "<h3>Calibration</h3>"
    + "<p id=\"calibration-progress\" role=\"status\"></p>"
    + "<p class=\"calibration-actions\">"
    + "<button type=\"button\" id=\"calibration-capture\">Capture now</button>"
    + "<button type=\"button\" id=\"calibration-finish\">Finish</button>"
    + "<button type=\"button\" id=\"calibration-cancel\">Cancel</button>"
    + "<button type=\"button\" id=\"calibration-exit\" hidden>Exit large view</button>"
    + "</p>"
    + "<p id=\"calibration-result\"></p>"
    + "<p id=\"calibration-done\" class=\"calibration-actions\" hidden>"
    + "<button type=\"button\" id=\"calibration-save\">Save</button>"
    + "<button type=\"button\" id=\"calibration-discard\">Discard</button>"
    + "</p>";
  mount.appendChild(panel);

  const progress = panel.querySelector("#calibration-progress");
  const result = panel.querySelector("#calibration-result");
  const doneRow = panel.querySelector("#calibration-done");
  const exitLarge = panel.querySelector("#calibration-exit");
  const stage = video.parentElement;
  let timer = 0;
  let dialog = null;
  let largeDismissed = false;
  let overlayBody = null;

  // ================================================================================

  function enterLarge() {
    document.body.classList.add("calibration-large");
    if (stage != null) {
      stage.classList.add("calibration-stage");
    }
    exitLarge.hidden = false;
    placeCanvas(video, canvas);
  }

  // ================================================================================

  function leaveLarge() {
    document.body.classList.remove("calibration-large");
    if (stage != null) {
      stage.classList.remove("calibration-stage");
    }
    exitLarge.hidden = true;
    placeCanvas(video, canvas);
  }

  // ================================================================================

  function syncLarge(capturing, computing) {
    if ((capturing || computing) && largeDismissed == false) {
      enterLarge();
      return;
    }
    if (capturing == false && computing == false) {
      largeDismissed = false;
      leaveLarge();
    }
  }

  // ================================================================================

  function stopPoll() {
    if (timer != 0) {
      window.clearInterval(timer);
      timer = 0;
    }
  }

  // ================================================================================

  function formatResult(body) {
    const summary = body && body.result;
    if (summary == null) {
      result.textContent = "";
      return;
    }
    const lines = [];
    if (typeof summary.focal_px === "number") {
      lines.push("Focal length " + summary.focal_px.toFixed(1) + " px");
    }
    if (body.stereo && typeof summary.baseline_mm === "number") {
      lines.push("Baseline " + summary.baseline_mm.toFixed(1) + " mm");
    }
    if (typeof summary.rms_left_px === "number") {
      lines.push("Left error " + summary.rms_left_px.toFixed(3) + " px");
    }
    if (body.stereo && typeof summary.rms_right_px === "number") {
      lines.push("Right error " + summary.rms_right_px.toFixed(3) + " px");
    }
    if (body.stereo && typeof summary.rms_stereo_px === "number") {
      lines.push("Stereo error " + summary.rms_stereo_px.toFixed(3) + " px");
    }
    if (summary.stale === true) {
      lines.push("Saved calibration does not match the live camera");
    }
    result.textContent = lines.join(". ");
  }

  // ================================================================================

  async function refresh() {
    const response = await fetch("/api/calibration");
    if (response.ok == false) {
      progress.textContent = "Calibration is not available";
      return;
    }
    const body = await response.json();
    const state = body.state || "CALIBRATION_IDLE";
    const accepted = body.accepted || 0;
    const target = body.target_count || 20;
    progress.textContent = (body.message || state) + " (" + accepted + " / " + target + ")";
    formatResult(body);
    const capturing = state === "CALIBRATION_CAPTURING";
    const computing = state === "CALIBRATION_COMPUTING";
    const finished = state === "CALIBRATION_DONE" || state === "CALIBRATION_FAILED";
    panel.querySelector("#calibration-capture").disabled = capturing == false;
    panel.querySelector("#calibration-finish").disabled = body.can_finish != true;
    panel.querySelector("#calibration-cancel").hidden = capturing == false && computing == false;
    doneRow.hidden = state !== "CALIBRATION_DONE";
    syncLarge(capturing, computing);
    overlayBody = capturing ? body : null;
    drawOverlay(video, canvas, overlayBody);
    if (capturing == false && computing == false) {
      stopPoll();
    }
    if (finished && state === "CALIBRATION_FAILED") {
      progress.textContent = body.message || "Calibration failed";
    }
  }

  // ================================================================================

  function startPoll() {
    stopPoll();
    timer = window.setInterval(() => {
      return refresh().catch(() => {
        progress.textContent = "Calibration status failed";
      });
    }, 400);
  }

  // ================================================================================

  function closeDialog() {
    if (dialog != null) {
      dialog.remove();
      dialog = null;
    }
  }

  // ================================================================================

  function openDialog() {
    closeDialog();
    dialog = document.createElement("div");
    dialog.className = "calibration-dialog";
    dialog.setAttribute("role", "dialog");
    dialog.setAttribute("aria-modal", "true");
    dialog.setAttribute("aria-labelledby", "calibration-title");
    dialog.innerHTML = ""
      + "<form class=\"calibration-card\" id=\"calibration-form\">"
      + "<h2 id=\"calibration-title\">Calibrate camera</h2>"
      + "<p>Print at 100% scale, then measure one square before starting.</p>"
      + "<label>Inner corners across<input id=\"calibration-cols\" type=\"number\" min=\"4\" max=\"20\" value=\"9\" required></label>"
      + "<label>Inner corners down<input id=\"calibration-rows\" type=\"number\" min=\"4\" max=\"20\" value=\"6\" required></label>"
      + "<label>Square size (mm)<input id=\"calibration-square\" type=\"number\" min=\"5\" max=\"100\" step=\"0.1\" value=\"25\" required></label>"
      + "<label>Paper<select id=\"calibration-paper\"><option value=\"a4\" selected>A4</option><option value=\"letter\">Letter</option><option value=\"a3\">A3</option></select></label>"
      + "<label>Orientation<select id=\"calibration-orient\"><option value=\"landscape\" selected>Landscape</option><option value=\"portrait\">Portrait</option></select></label>"
      + "<p id=\"calibration-form-error\" role=\"alert\"></p>"
      + "<div class=\"calibration-actions\">"
      + "<button type=\"button\" id=\"calibration-pdf\">Download PDF</button>"
      + "<button type=\"submit\">Start</button>"
      + "<button type=\"button\" id=\"calibration-close\">Close</button>"
      + "</div></form>";
    document.body.appendChild(dialog);
    const form = dialog.querySelector("#calibration-form");
    const error = dialog.querySelector("#calibration-form-error");
    const cols = dialog.querySelector("#calibration-cols");
    const rows = dialog.querySelector("#calibration-rows");
    const square = dialog.querySelector("#calibration-square");
    const paper = dialog.querySelector("#calibration-paper");
    const orient = dialog.querySelector("#calibration-orient");

    // ================================================================================

    function applyFit() {
      const squareMm = fieldNumber(square);
      const page = pagePoints(paper.value, orient.value === "landscape");
      const layout = squareMm == null ? null : fitSquares(page, squareMm);
      if (layout == null) {
        error.textContent = "That square does not fit this page. Use a smaller square or a larger page.";
        return null;
      }
      cols.value = String(layout.cols - 1);
      rows.value = String(layout.rows - 1);
      error.textContent = "";
      return { page: page, layout: layout };
    }

    dialog.querySelector("#calibration-pdf").addEventListener("click", () => {
      const fitted = applyFit();
      if (fitted == null) {
        return;
      }
      downloadBoard(fitted.page, fitted.layout, {
        paper: paper.value,
        landscape: orient.value === "landscape",
        squareMm: fieldNumber(square)
      });
    });
    paper.addEventListener("change", applyFit);
    orient.addEventListener("change", applyFit);
    square.addEventListener("change", applyFit);
    dialog.querySelector("#calibration-close").addEventListener("click", closeDialog);
    dialog.addEventListener("keydown", (event) => {
      if (event.key === "Escape") {
        closeDialog();
      }
    });
    form.addEventListener("submit", async (event) => {
      event.preventDefault();
      const innerCols = fieldNumber(cols);
      const innerRows = fieldNumber(rows);
      const squareMm = fieldNumber(square);
      if (innerCols == null || innerRows == null || squareMm == null
          || innerCols < 4 || innerCols > 20 || innerRows < 4 || innerRows > 20
          || squareMm < 5 || squareMm > 100) {
        error.textContent = "Corners must be 4 to 20, and the square 5 to 100 mm.";
        return;
      }
      error.textContent = "";
      const response = await fetch("/api/calibration", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          inner_cols: innerCols,
          inner_rows: innerRows,
          square_mm: squareMm
        })
      });
      if (response.ok == false) {
        error.textContent = response.status === 404
          ? "No live camera"
          : "Calibration did not start";
        return;
      }
      closeDialog();
      largeDismissed = false;
      panel.hidden = false;
      await refresh();
      startPoll();
    });
    cols.focus();
  }

  button.addEventListener("click", openDialog);
  panel.querySelector("#calibration-capture").addEventListener("click", async () => {
    await fetch("/api/calibration/capture", { method: "POST" });
    await refresh();
  });
  panel.querySelector("#calibration-finish").addEventListener("click", async () => {
    const response = await fetch("/api/calibration/finish", { method: "POST" });
    if (response.ok == false) {
      progress.textContent = "Need at least 10 snapshots";
      return;
    }
    startPoll();
    await refresh();
  });
  panel.querySelector("#calibration-cancel").addEventListener("click", async () => {
    await fetch("/api/calibration", { method: "DELETE" });
    stopPoll();
    largeDismissed = false;
    leaveLarge();
    drawOverlay(video, canvas, null);
    panel.hidden = true;
  });
  panel.querySelector("#calibration-save").addEventListener("click", async () => {
    const response = await fetch("/api/calibration/save", { method: "POST" });
    progress.textContent = response.ok ? "Calibration saved" : "Save failed";
    if (response.ok) {
      largeDismissed = false;
      leaveLarge();
      panel.hidden = true;
      drawOverlay(video, canvas, null);
    }
  });
  panel.querySelector("#calibration-discard").addEventListener("click", async () => {
    await fetch("/api/calibration", { method: "DELETE" });
    stopPoll();
    largeDismissed = false;
    leaveLarge();
    drawOverlay(video, canvas, null);
    panel.hidden = true;
  });
  exitLarge.addEventListener("click", () => {
    largeDismissed = true;
    leaveLarge();
  });
  window.addEventListener("keydown", (event) => {
    if (event.key !== "Escape" || dialog != null) {
      return;
    }
    if (document.body.classList.contains("calibration-large") == false) {
      return;
    }
    largeDismissed = true;
    leaveLarge();
  });
  window.addEventListener("resize", () => {
    if (panel.hidden == false) {
      drawOverlay(video, canvas, overlayBody);
    }
  });
}

// ================================================================================

bindCalibration();
