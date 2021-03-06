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

if not v then v = {} end
if not AQUARIA_VERSION then dofile("scripts/entities/entityinclude.lua") end

v.didInit = false
v.done = false
v.door = 0

function init(me)
	v.door = node_getNearestEntity(me, "EnergyDoor")
end

local function doInit(me)
	debugLog("Setting door to opened")
	--entity_setState(v.door, STATE_OPENED, -1, true)
	entity_setState(v.door, STATE_OPENED)
end

function update(me, dt)
	if not v.didInit then
if not AQUARIA_VERSION then dofile("scripts/entities/entityinclude.lua") end
		doInit(me)
		v.didInit = true
	end
	
	if not v.done then
if not AQUARIA_VERSION then dofile("scripts/entities/entityinclude.lua") end
		if getFlag(FLAG_SUNKENCITY_PUZZLE) < SUNKENCITY_BOSSDONE then
			if node_isEntityIn(me, getNaija()) then
				debugLog("closing door")
				entity_setState(v.door, STATE_CLOSE, -1, true)
				--if entity_isState(v.door, STATE_CLOSE) then
					--v.done = true			
				--end
				v.done = true
			end
		end
	end
end


