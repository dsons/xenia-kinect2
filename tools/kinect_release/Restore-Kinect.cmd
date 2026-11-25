@echo off
rem Gives the Kinect back to its default Windows driver. See Setup-Kinect.ps1.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Setup-Kinect.ps1" -Restore %*
