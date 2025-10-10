# Bash completion script for grk_decompress (version 14.2.0)
# just add to root directory as .grk_decompress_completion.bash
# and add this line to .bashrc
# source .grk_decompress_completion.bash
#
_grk_decompress_completion() {
    local cur prev words cword
    _init_completion -n = || return  # -n = prevents splitting on '=' for options with values

    # Base completion: all options and flags
    local opts="-a --out-dir -c --compression -d --region -e --repetitions -f --force-rgb -g --plugin-path -G --device-id -H --num-threads -i --in-file -k --kernel-build -l --layers -L --compression-level -m --random-access -o --out-file -O --out-fmt -p --precision -r --reduce -s --split-pnm -t --tile-index -u --upsample -v --verbose -V --transfer-exif-tags -W --log-file -X --xml -y --batch-src -z --duration -h --help"
    COMPREPLY=( $(compgen -W "$opts" -- "$cur") )

    # Context-specific completions based on the previous word
    case $prev in
        # Directory options
        -a|--out-dir|-y|--batch-src)
            COMPREPLY=( $(compgen -d -- "$cur") )
            return
            ;;
        # File options
        -i|--in-file|-o|--out-file|-g|--plugin-path|-W|--log-file)
            COMPREPLY=( $(compgen -f -- "$cur") )
            return
            ;;
        # Options with potential specific values (examples inferred; adjust as needed)
        -O|--out-fmt)
            # Assuming common formats supported by grk_decompress; update based on your code
            COMPREPLY=( $(compgen -W "png jpg tif raw pnm" -- "$cur") )
            return
            ;;
        -c|--compression)
            # Placeholder: add known compression types if defined in your codebase
            COMPREPLY=( $(compgen -W "lossless lossy" -- "$cur") )
            return
            ;;
        -r|--reduce)
            # Suggest a few valid reduction levels (0 to GRK_MAXRLVLS-1)
            COMPREPLY=( $(compgen -W "0 1 2 3 4 5" -- "$cur") )
            return
            ;;
        -l|--layers)
            # Suggest a few layer numbers (1 to maxNumLayersJ2K)
            COMPREPLY=( $(compgen -W "1 2 3 4 5" -- "$cur") )
            return
            ;;
    esac

    # If current word starts with '-', suggest options only (no files yet)
    if [[ "$cur" == -* ]]; then
        COMPREPLY=( $(compgen -W "$opts" -- "$cur") )
    fi
}

complete -F _grk_decompress_completion grk_decompress
