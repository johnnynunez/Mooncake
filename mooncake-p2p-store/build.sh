#!/bin/bash

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 TARGET"
    exit 1
fi

TARGET=$1
cd "src/p2pstore"
if [ $? -ne 0 ]; then
    echo "Error: Directory src/p2pstore does not exist."
    exit 1
fi

go get
if [ $? -ne 0 ]; then
    echo "Error: Failed to get dependencies."
    exit 1
fi

go build -o "$TARGET/p2p-store-example" "../example/p2p-store-example.go"
if [ $? -ne 0 ]; then
    echo "Error: Failed to build the example."
    exit 1
fi

echo "Build completed successfully."
