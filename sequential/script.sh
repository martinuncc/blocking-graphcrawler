#!/bin/bash
#SBATCH --job-name=blocking
#SBATCH --partition=Centaurus
#SBATCH --time=00:10:00
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --mem=10G

make
./client "Tom Hanks" 1
./client "Tom Hanks" 4
./blocking "Tom Hanks" 1
./blocking "Tom Hanks" 4
