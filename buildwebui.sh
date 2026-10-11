#!/bin/bash

cd webui
rm -rf node_modules
npm install
npm run format
npm run check
npm test
npm run build
