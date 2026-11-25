@echo off
rem One-time Kinect driver setup for Xenia. See Setup-Kinect.ps1.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Setup-Kinect.ps1" %*
