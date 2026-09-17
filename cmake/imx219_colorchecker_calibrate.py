#!/usr/bin/env python3
"""Calibrate the DCMIPP YUV color-conversion matrix against a standard
24-patch ColorChecker (X-Rite ColorChecker Classic), instead of tuning by
eye like imx219_yuv_matrix_tune.py.

How it works:
  1. Resets the live hardware matrix (see imx219_yuv_matrix_tune.py's module
     docstring for the register layout/addresses) to the known compile-time
     baseline from Src/app_cam.c's CAM_EnableYuv() -- calibration has to
     start from a known matrix, not whatever was last hand-tuned.
  2. Grabs a frame from the board's UVC video device (OpenCV), and asks you
     to click the CENTERS of the four corner patches (top-left, top-right,
     bottom-right, bottom-left) of the chart in the image. A homography
     from those four points locates all 24 patch centers, handling any
     camera-relative tilt/rotation of the chart, without needing exact chart
     bezel dimensions.
  3. Averages a small box at each of the 24 patch centers ("observed"
     colors) and compares them to the ColorChecker's standard published
     sRGB values ("reference" colors, REFERENCE_SRGB below).
  4. Solves a 3x3 color-correction matrix (CCM, least squares) mapping
     observed -> reference.
  5. Composes CCM into the baseline matrix: M_final = M0 @ CCM. This keeps
     the correction mathematically folded into the existing RGB->YUV
     hardware encode, the same way imx219_yuv_matrix_tune.py's column
     scaling did (a diagonal CCM is a special case of this) -- so a
     standard YUV->RGB decode on the host still recovers CCM @ raw_rgb,
     assuming the encode/decode round trip is close to linear (it isn't
     perfectly, due to 8-bit clamping/rounding, but close enough for
     correcting a systematic cast).
  6. Writes M_final live to the hardware registers for instant preview, and
     prints it ready to paste into CAM_EnableYuv() in Src/app_cam.c.
  7. Re-captures at the same chart position and reports the residual
     error, so you can see numerically whether the fit actually helped.

This is a practical, "does it look/measure right" calibration (like the
rest of this project's tuning), not colorimetrically rigorous (no gamma
linearization, no illuminant adaptation) -- but it replaces guessing with an
actual least-squares fit against real reference colors.

Setup:
  1. Build and flash normally, then start the UVC stream (this triggers
     CAM_EnableYuv(), which must have run at least once for the hardware
     registers to hold anything meaningful).
  2. Find the board's video device: `v4l2-ctl --list-devices` (NOT your
     laptop's built-in camera or any other webcam -- double check the
     device path).
  3. Point the camera at a well and evenly lit X-Rite ColorChecker Classic
     (or equivalent 24-patch chart with the same standard layout), filling
     as much of the frame as practical, straight-on if possible.
  4. Run:
       pip install pyocd opencv-python numpy
       python3 cmake/imx219_colorchecker_calibrate.py --device /dev/videoN
  5. In the preview window, press SPACE to capture a frame once the chart
     is well framed, or ESC to cancel.
  6. Click the four corner patch centers as prompted (in order: top-left,
     top-right, bottom-right, bottom-left).
  7. Check the annotated capture saved to colorchecker_capture.png -- the
     numbered sample boxes should each land inside a real patch, not on a
     gap or the chart's border. If they don't, the geometry/order was
     probably off; re-run and re-click more carefully.
  8. Review the printed observed-vs-reference table and the final matrix,
     then paste the printed CAM_EnableYuv() block into Src/app_cam.c.

Risk note: like imx219_yuv_matrix_tune.py, this writes directly to a live
DCMIPP hardware pipeline register while it may be actively converting
frames. This script only performs two such writes (baseline reset, then
the final matrix) rather than many rapid interactive ones, which is lower
risk, but a board lockup requiring a physical reset is still possible.

Requires: pip install pyocd opencv-python numpy
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from imx219_yuv_matrix_tune import connect, read_all, write_all, MATRIX_FIELDS  # noqa: E402

# Compile-time baseline from Src/app_cam.c's CAM_EnableYuv() -- calibration
# is always computed relative to this known matrix, not any leftover
# hand-tuned state from imx219_yuv_matrix_tune.py.
DEFAULT_MATRIX = {
    "RR": 131, "RG": -119, "RB": -12, "RA": 128,
    "GR": 55, "GG": 183, "GB": 18, "GA": 0,
    "BR": -30, "BG": -101, "BB": 131, "BA": 128,
}

# X-Rite ColorChecker Classic, standard published sRGB averages (D65),
# 4 rows x 6 columns, row-major from top-left (patch A1) to bottom-right
# (patch D6). Widely reproduced reference values (e.g. babelcolor.com).
REFERENCE_SRGB = np.array([
    [115, 82, 68], [194, 150, 130], [98, 122, 157], [87, 108, 67], [133, 128, 177], [103, 189, 170],
    [214, 126, 44], [80, 91, 166], [193, 90, 99], [94, 60, 108], [157, 188, 64], [224, 163, 46],
    [56, 61, 150], [70, 148, 73], [175, 54, 60], [231, 199, 31], [187, 86, 149], [8, 133, 161],
    [243, 243, 242], [200, 200, 200], [160, 160, 160], [122, 122, 121], [85, 85, 85], [52, 52, 52],
], dtype=np.float64)

GRID_COLS, GRID_ROWS = 6, 4
PATCH_SAMPLE_RADIUS = 8  # px, half-width of the averaging box at each patch center
MATRIX_COEFF_LIMIT = 1023  # documented signed range for RR/RG/RB/GR/GG/GB/BR/BG/BB


def matrix_dict_to_array(m):
    return np.array([[m["RR"], m["RG"], m["RB"]],
                      [m["GR"], m["GG"], m["GB"]],
                      [m["BR"], m["BG"], m["BB"]]], dtype=np.float64)


def array_to_matrix_dict(a, base):
    out = dict(base)
    out["RR"], out["RG"], out["RB"] = (int(round(v)) for v in a[0])
    out["GR"], out["GG"], out["GB"] = (int(round(v)) for v in a[1])
    out["BR"], out["BG"], out["BB"] = (int(round(v)) for v in a[2])
    return out


def clip_matrix(m):
    clipped = False
    out = dict(m)
    for field in MATRIX_FIELDS:
        if abs(out[field]) > MATRIX_COEFF_LIMIT:
            print(f"warning: {field}={out[field]} exceeds +-{MATRIX_COEFF_LIMIT}, clipping "
                  f"(fit is likely poor -- check chart lighting/framing)", file=sys.stderr)
            out[field] = max(-MATRIX_COEFF_LIMIT, min(MATRIX_COEFF_LIMIT, out[field]))
            clipped = True
    return out, clipped


def capture_frame(device):
    import cv2

    cap = cv2.VideoCapture(device)
    if not cap.isOpened():
        raise RuntimeError(f"could not open video device '{device}'")

    win = "Press SPACE to capture, ESC to cancel"
    cv2.namedWindow(win)
    frame = None
    try:
        while True:
            ok, img = cap.read()
            if not ok:
                raise RuntimeError(f"failed to read a frame from '{device}'")
            cv2.imshow(win, img)
            key = cv2.waitKey(1) & 0xFF
            if key == 27:
                raise KeyboardInterrupt("capture cancelled")
            if key == ord(' '):
                frame = img
                break
    finally:
        cap.release()
        cv2.destroyWindow(win)
    return frame


def pick_corners(frame):
    import cv2

    points = []
    disp = frame.copy()
    win = "Click patch centers: top-left -> top-right -> bottom-right -> bottom-left"
    labels = ["top-left", "top-right", "bottom-right", "bottom-left"]

    def on_click(event, x, y, flags, param):
        if event == cv2.EVENT_LBUTTONDOWN and len(points) < 4:
            points.append((x, y))
            cv2.circle(disp, (x, y), 5, (0, 255, 0), -1)
            cv2.putText(disp, labels[len(points) - 1], (x + 8, y), cv2.FONT_HERSHEY_SIMPLEX,
                        0.5, (0, 255, 0), 1)
            cv2.imshow(win, disp)

    cv2.namedWindow(win)
    cv2.setMouseCallback(win, on_click)
    cv2.imshow(win, disp)
    print("Click the CENTER of each corner patch, in order: top-left, top-right, bottom-right, bottom-left.")
    while len(points) < 4:
        if cv2.waitKey(20) & 0xFF == 27:
            raise KeyboardInterrupt("corner selection cancelled")
    cv2.waitKey(300)
    cv2.destroyWindow(win)
    return np.array(points, dtype=np.float32)


def sample_patches(frame, corners):
    import cv2

    src = np.array([[0, 0], [GRID_COLS - 1, 0], [GRID_COLS - 1, GRID_ROWS - 1], [0, GRID_ROWS - 1]],
                    dtype=np.float32)
    homography = cv2.getPerspectiveTransform(src, corners)
    grid_pts = np.array([[c, r] for r in range(GRID_ROWS) for c in range(GRID_COLS)], dtype=np.float32)
    pixel_pts = cv2.perspectiveTransform(grid_pts.reshape(-1, 1, 2), homography).reshape(-1, 2)

    rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    h, w = rgb_frame.shape[:2]
    observed = np.zeros((GRID_ROWS * GRID_COLS, 3), dtype=np.float64)
    annotated = frame.copy()
    for i, (px, py) in enumerate(pixel_pts):
        px, py = int(round(px)), int(round(py))
        x0, x1 = max(0, px - PATCH_SAMPLE_RADIUS), min(w, px + PATCH_SAMPLE_RADIUS)
        y0, y1 = max(0, py - PATCH_SAMPLE_RADIUS), min(h, py + PATCH_SAMPLE_RADIUS)
        patch = rgb_frame[y0:y1, x0:x1].reshape(-1, 3)
        observed[i] = patch.mean(axis=0) if patch.size else np.nan
        cv2.rectangle(annotated, (x0, y0), (x1, y1), (0, 255, 0), 1)
        cv2.putText(annotated, str(i + 1), (px - 6, py - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.4, (0, 255, 255), 1)
    return observed, annotated, pixel_pts


def solve_ccm(observed, reference):
    # reference_i ~= CCM @ observed_i (column-vector convention, matching
    # how the hardware matrix applies: Y = RR*R + RG*G + RB*B, etc.)
    # => reference ~= observed @ CCM.T (row-vector / N x 3 form)
    ccm_t, *_ = np.linalg.lstsq(observed, reference, rcond=None)
    return ccm_t.T


def report_error(observed, reference, label):
    err = observed - reference
    mae = np.abs(err).mean()
    rmse = np.sqrt((err ** 2).mean())
    print(f"{label}: mean abs error={mae:.1f}, rmse={rmse:.1f} (0-255 scale)")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--device", required=True, help="UVC video device for the board, e.g. /dev/video2")
    parser.add_argument("--probe-uid", default=None, help="ST-Link probe unique ID, if more than one is attached")
    args = parser.parse_args()

    try:
        import cv2  # noqa: F401
    except ImportError:
        print("error: opencv-python not installed. Run: pip install opencv-python", file=sys.stderr)
        return 1

    try:
        session = connect(args.probe_uid)
    except RuntimeError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    with session:
        target = session.target

        print("Resetting live hardware matrix to the Src/app_cam.c baseline before calibrating...")
        write_all(target, DEFAULT_MATRIX)

        try:
            frame = capture_frame(args.device)
            corners = pick_corners(frame)
        except KeyboardInterrupt as e:
            print(f"\n{e}", file=sys.stderr)
            return 1
        except RuntimeError as e:
            print(f"error: {e}", file=sys.stderr)
            return 1

        observed, annotated, _ = sample_patches(frame, corners)
        import cv2
        cv2.imwrite("colorchecker_capture.png", annotated)
        print("Saved colorchecker_capture.png -- check that each numbered box lands inside a real patch.")

        print("\n#   observed(R,G,B)         reference(R,G,B)")
        for i in range(24):
            o = observed[i]
            r = REFERENCE_SRGB[i]
            print(f"{i + 1:2d}  ({o[0]:5.1f},{o[1]:5.1f},{o[2]:5.1f})   ({r[0]:5.1f},{r[1]:5.1f},{r[2]:5.1f})")

        report_error(observed, REFERENCE_SRGB, "\nBefore correction")

        ccm = solve_ccm(observed, REFERENCE_SRGB)
        print("\nSolved color-correction matrix (observed RGB -> reference RGB):")
        print(ccm)

        m0 = matrix_dict_to_array(DEFAULT_MATRIX)
        m_final = m0 @ ccm
        final_matrix, clipped = clip_matrix(array_to_matrix_dict(m_final, DEFAULT_MATRIX))

        print("\nWriting corrected matrix to live hardware registers for preview...")
        write_all(target, final_matrix)

        print("\nRe-capture the SAME chart position to verify (press SPACE again, ESC to skip)...")
        try:
            frame2 = capture_frame(args.device)
            observed2, annotated2, _ = sample_patches(frame2, corners)
            cv2.imwrite("colorchecker_capture_after.png", annotated2)
            report_error(observed2, REFERENCE_SRGB, "After correction")
        except (KeyboardInterrupt, RuntimeError):
            print("(skipped verification capture)")

        print("\nPaste into CAM_EnableYuv() in Src/app_cam.c:\n")
        print("  DCMIPP_ColorConversionConfTypeDef color_conf = {")
        print("    .ClampOutputSamples = ENABLE,")
        print("    .OutputSamplesType = DCMIPP_CLAMP_YUV,")
        print(f"    .RR = {final_matrix['RR']}, .RG = {final_matrix['RG']}, "
              f".RB = {final_matrix['RB']}, .RA = {final_matrix['RA']},")
        print(f"    .GR = {final_matrix['GR']}, .GG = {final_matrix['GG']}, "
              f".GB = {final_matrix['GB']}, .GA = {final_matrix['GA']},")
        print(f"    .BR = {final_matrix['BR']}, .BG = {final_matrix['BG']}, "
              f".BB = {final_matrix['BB']}, .BA = {final_matrix['BA']},")
        print("  };")
        if clipped:
            print("\nNote: one or more coefficients were clipped to the valid range -- the fit may be "
                  "poor (check lighting/framing) or the correction genuinely extreme.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
