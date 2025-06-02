#!/bin/bash

# Format all C source files using clang-format
echo "Formatting C source files..."

# Find all .c and .h files and format them
find . -name "*.c" -o -name "*.h" | while read -r file; do
    echo "Formatting: $file"
    clang-format -i -style=file "$file"
done

echo "Formatting complete!"
