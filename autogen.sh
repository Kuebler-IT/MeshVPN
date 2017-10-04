#!/bin/bash
#
# MeshVPN - A open source peer-to-peer VPN (forked from PeerVPN)
#
# Copyright (C) 2012-2016  Tobias Volk <mail@tobiasvolk.de>
# Copyright (C) 2016       Hideman Developer <company@hideman.net>
# Copyright (C) 2017       Benjamin Kübler <b.kuebler@kuebler-it.de>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

aclocal
autoconf
autoheader
automake --add-missing
