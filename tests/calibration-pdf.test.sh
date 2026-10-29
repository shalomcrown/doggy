#!/usr/bin/env bash
# The printed chessboard carries a caption under the grid.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
node --input-type=module -e '
import fs from "fs";
const root = process.argv[1];
const source = fs.readFileSync(root + "/web/calibration.js", "utf8")
    .replace("bindCalibration();", "");
const fn = new Function(source + "\nreturn { pagePoints, fitSquares, buildPdf };");
const api = fn();
const page = api.pagePoints("a4", true);
const layout = api.fitSquares(page, 25);
const details = { paper: "a4", landscape: true, squareMm: 25 };
if (layout == null || layout.cols !== 10 || layout.rows !== 7) {
  console.log("FAIL A4 landscape 25 mm stays 10 by 7 squares");
  process.exit(1);
}
const pdf = api.buildPdf(page, layout, details);
const boardBottom = page.h - layout.margin - layout.rows * layout.square;
const positions = [...pdf.matchAll(/1 0 0 1 [\d.]+ ([\d.]+) Tm/g)].map((match) => Number(match[1]));
const below = positions.every((y) => y < boardBottom - 8);
const onPage = positions.length === 2 && positions.every((y) => y > 18);
const wanted = [
  "A4 landscape. Print at 100% scale. Do not fit to page.",
  "10 x 7 squares, 25 mm each. Board 250 x 175 mm. Inner corners 9 x 6."
];
const missing = wanted.filter((line) => pdf.includes(line) == false);
if (pdf.includes("/BaseFont /Helvetica") == false || below == false || onPage == false || missing.length > 0) {
  console.log("FAIL caption sits under the board with paper, square, and board size");
  console.log(missing.join("\n"));
  console.log(positions.join(","));
  process.exit(1);
}
console.log("PASS caption sits under the board with paper, square, and board size");
' "$ROOT"
