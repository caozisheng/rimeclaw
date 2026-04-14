---
name: test-calc-py
description: Calculate a math expression using a Python script
always: true
commands:
  - name: calc-py
    description: Calculate a math expression using Python
    tool_name: exec
    arg_mode: freeform
---

# Python Calculator Skill

When the user asks you to calculate something, use the `exec` tool to run the
Python calculator script at `{{SKILL_DIR}}/calc.py`.

For example, to calculate 13*29, run:
`python "{{SKILL_DIR}}/calc.py" "13*29"`

Always use the exec tool with this script for calculations. Do not compute in your head.
