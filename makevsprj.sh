#!/bin/bash
mkdir -p vs
xmake project -k vs2019 -a "x64" -m "release" vs
