#!/bin/sh


fullscreenshot() {
    timestamp="$(date +%m%d%H%M%S)"
    save_path="$HOME/Pictures/Screenshots/Screenshot-${timestamp}.png"
    wayshot --stdout | swappy -f - -o "${save_path}"
    wl-copy < "${save_path}"
    if [ -s "${save_path}" ]; then
        notify-send --icon="${save_path}" -a "Screenshot" "Full" "Screenshot in ${save_path}"
    else
        rm "${save_path}"
    fi
}

selectarea() {
    timestamp="$(date +%m%d%H%M%S)"
    save_path="$HOME/Pictures/Screenshots/Screenshot-${timestamp}.png"
    wayshot -s "$(slurp -f '%x %y %w %h')" --stdout | swappy -f - -o "${save_path}"
    wl-copy < "${save_path}"
    if [ -s "${save_path}" ]; then
        notify-send --icon="${save_path}" -a "Screenshot" "Area Select" "Screenshot in ${save_path}"
    else
        rm "${save_path}"
    fi
}

whichMode(){
    if [ "$1" = "full" ] || [ "$1" = "" ]; then
        fullscreenshot
    elif [ "$1" = "area" ]; then
        selectarea
    else
        notify-send -u "critical" -a "Screenshot" "Error" "Please select 'area' or 'full'"
    fi
}

whichMode "$@"
