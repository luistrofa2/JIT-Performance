#!/bin/bash

for dir in */ ; do
    cp -v php.ini "$dir"
done