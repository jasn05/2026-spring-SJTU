# DSP Labs

This repository contains lab reports and related materials for the Digital Signal Processing course in 2026 Spring.

## Contents

| Folder | Main files | Topic |
| --- | --- | --- |
| `lab1/` | `lab1.md`, `lab1.pdf`, figures | LTI system response, convolution, overlap-add processing, and sample-by-sample implementation |
| `lab2/` | `lab2.md`, `lab2.pdf`, figures | Parametric equalizer design and multiple notch filter design |
| `lab3/` | `lab3.md`, `lab3.pdf`, figures | Windowing effects on spectral analysis using rectangular and Hamming windows |
| `lab4/` | `lab4.md`, `lab4.pdf`, `untitled.m`, figures | IIR/FIR filter analysis and Butterworth/FIR band-stop filter design |

## Structure

Each lab folder keeps the original report source, exported PDF, and generated figures together:

```text
lab1/
lab2/
lab3/
lab4/
```

The Markdown files are the editable reports. The PDF files are exported versions for submission or review.

## Requirements

The code examples in the reports are written for MATLAB. Some scripts use Signal Processing Toolbox functions such as:

- `freqz`
- `impz`
- `zplane`
- `butter`
- `fir1`

## Notes

- Image files are kept next to the corresponding Markdown report so relative image links render correctly.
- `lab4/untitled.m` contains MATLAB code associated with Lab 4.
- This repository is organized as coursework material; generated figures and PDFs are intentionally included.
