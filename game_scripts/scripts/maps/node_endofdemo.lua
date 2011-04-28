-- Copyright (C) 2007, 2010 - Bit-Blot
--
-- This file is part of Aquaria.
--
-- Aquaria is free software; you can redistribute it and/or
-- modify it under the terms of the GNU General Public License
-- as published by the Free Software Foundation; either version 2
-- of the License, or (at your option) any later version.
--
-- This program is distributed in the hope that it will be useful,
-- but WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
--
-- See the GNU General Public License for more details.
--
-- You should have received a copy of the GNU General Public License
-- along with this program; if not, write to the Free Software
-- Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

v = getVars()

v.n = 0
v.done = false

function init(me)
	v.n = getNaija()
end

function update(me, dt)
	if not v.done and node_isEntityIn(me, v.n) then
		v.done = true
		if isDemo() then
			entity_idle(v.n)
			setGameSpeed(0.5, 1)
			watch(1)
			centerText(getStringBank(850))
			fade2(1, 3)
			fadeOutMusic(3)
			watch(3)
			watch(0.5)
			quit()
		end
	end
end