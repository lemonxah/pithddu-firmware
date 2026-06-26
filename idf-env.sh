# Source this to activate ESP-IDF v5.5.4 (eim-managed) on this machine.
#   source ./idf-env.sh
# Then: idf.py set-target esp32s3 / idf.py build / idf.py -p /dev/ttyACM0 flash
. "$HOME/.espressif/tools/activate_idf_v5.5.4.sh" >/dev/null 2>&1
unalias idf.py 2>/dev/null
idf.py() { "$IDF_PYTHON_ENV_PATH/bin/python" "$IDF_PATH/tools/idf.py" "$@"; }
