@echo off
cd /d "%~dp0"
echo Starting mock PS4 agent on port 8741...
python mock_agent.py
pause
