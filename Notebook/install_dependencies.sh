#!/bin/bash
set -e

cd ..

source venv/bin/activate
pip install seaborn matplotlib pandas numpy
deactivate

cd Notebook
