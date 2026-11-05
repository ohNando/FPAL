#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# Kept for one release so that "sudo /usr/share/fpal/authselect-fpal.sh install"
# in old notes keeps working. The work is done by platform.sh.
exec "$(dirname "$0")/platform.sh" authselect "$@"
