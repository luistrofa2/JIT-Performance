#!/bin/bash

for dir in */ ; do
    cp -v Makefile "$dir"
done