#!/bin/bash
mkdir -p android/app/src/main/assets/hkclock
rm -fr android/app/src/main/assets/hkclock/*
cp -r web/* android/app/src/main/assets/hkclock/
