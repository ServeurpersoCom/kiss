#!/bin/bash

npm ci
npm run format
npm run check
npm test
npm run build
