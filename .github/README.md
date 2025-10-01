# GitHub Pages Deployment

This directory contains the GitHub Actions workflow for deploying RobusText to GitHub Pages.

## Setup

1. Enable GitHub Pages in your repository:
   - Go to Settings → Pages
   - Source: GitHub Actions

2. Push to main branch to trigger deployment

The workflow will:
- Build the WebAssembly version using Emscripten
- Create a deployment with the optimized build
- Deploy to GitHub Pages

## Access

After deployment, the editor will be available at:
```
https://georgebaskervil.github.io/RobusText/
```

The default landing page loads the simple combining characters test file.

## URLs

- Main editor with test file: `https://georgebaskervil.github.io/RobusText/`
- Empty editor: `https://georgebaskervil.github.io/RobusText/RobusText.html`
- 10k combining test: `https://georgebaskervil.github.io/RobusText/RobusText.html?file=/testdata/combining_10k.txt`
- Simple combining test: `https://georgebaskervil.github.io/RobusText/RobusText.html?file=/testdata/simple_combining.txt`
