#!/bin/bash
set -e

cd "$(dirname "$0")"
npm ci
npm run format
npm run check
npm test
npm run build
