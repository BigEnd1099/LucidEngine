#!/bin/bash
cd engine/build
./engine --headless &
ENGINE_PID=$!
sleep 2
node /app/applet/test_gltf.mjs
kill -9 $ENGINE_PID
