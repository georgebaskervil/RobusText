#!/bin/bash

echo "Setting up code formatting for RobusText..."

# Check if clang-format is installed
if ! command -v clang-format &> /dev/null; then
    echo "Installing clang-format..."
    if command -v brew &> /dev/null; then
        brew install clang-format
    else
        echo "Please install clang-format manually or install Homebrew first"
        exit 1
    fi
else
    echo "✓ clang-format is already installed"
fi

# Check if pre-commit is installed
if ! command -v pre-commit &> /dev/null; then
    echo "Installing pre-commit..."
    pip3 install pre-commit
else
    echo "✓ pre-commit is already installed"
fi

# Initialize git repository if not already initialized
if [ ! -d ".git" ]; then
    echo "Initializing git repository..."
    git init
    git add .
    git commit -m "Initial commit"
fi

# Install pre-commit hooks
echo "Installing pre-commit hooks..."
pre-commit install

echo ""
echo "✓ Setup complete!"
echo ""
echo "Available commands:"
echo "  make format         - Format all C source files"
echo "  make check-format   - Check if code is properly formatted"
echo "  make install-hooks  - Install/reinstall pre-commit hooks"
echo ""
echo "Pre-commit hooks are now active and will run automatically on git commit."
echo "To manually run all hooks: pre-commit run --all-files"
