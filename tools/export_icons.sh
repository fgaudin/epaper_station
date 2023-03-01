#!/bin/bash
# usage ./export_icons.sh -s <size>

DAY_NIGHT=('d' 'n')
ICONS=('01' '02' '03' '04' '09' '10' '11' '13' '50')

while getopts s: flag
do
    case "${flag}" in
        s) size=${OPTARG};;
    esac
done

for end in "${DAY_NIGHT[@]}"
do
  for icon in "${ICONS[@]}"
  do
    convert icons/$icon$end.svg -resize ${size}x${size} -remap icons/eink___epaper_eink-2color.png -monochrome BMP3:data/${icon}${end}_${size}.bmp
  done
done

