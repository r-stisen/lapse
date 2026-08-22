# LAPSE - Language-Agnostic subtitle synchronization engine
# Copyright (C) 2026 Rasmus Stisen Jensen (rs-jensen)
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.

import os
import sys
import urllib.request

if os.environ.get("WEB", "1") != "1":
    sys.exit(0)

url = "http://127.0.0.1:%s/api/state" % os.environ.get("WEB_PORT", "8080")

try:
    with urllib.request.urlopen(url, timeout=5) as answer:
        sys.exit(0 if answer.status == 200 else 1)
except Exception as e:
    print(e)
    sys.exit(1)
