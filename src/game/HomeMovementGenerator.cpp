/*
 * Copyright (C) 2005-2010 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "HomeMovementGenerator.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "Traveller.h"
#include "DestinationHolderImp.h"
#include "ObjectMgr.h"
#include "WorldPacket.h"

void
HomeMovementGenerator<Creature>::Initialize(Creature & owner)
{
    owner.RemoveSplineFlag(SPLINEFLAG_WALKMODE);
    _setTargetLocation(owner);
}

void
HomeMovementGenerator<Creature>::Reset(Creature &)
{
}

void
HomeMovementGenerator<Creature>::_setTargetLocation(Creature & owner)
{
    if (owner.hasUnitState(UNIT_STAT_NOT_MOVE))
        return;

    float x, y, z;

    // at apply we can select more nice return points base at current movegen
    if (owner.GetMotionMaster()->empty() || !owner.GetMotionMaster()->top()->GetResetPosition(owner,x,y,z))
        owner.GetRespawnCoord(x, y, z);

    CreatureTraveller traveller(owner);

    uint32 travel_time = i_destinationHolder.SetDestination(traveller, x, y, z);
    modifyTravelTime(travel_time);
    owner.clearUnitState(UNIT_STAT_ALL_STATE);
}

bool
HomeMovementGenerator<Creature>::Update(Creature &owner, const uint32& time_diff)
{
    CreatureTraveller traveller( owner);
    if (i_destinationHolder.UpdateTraveller(traveller, time_diff, false))
    {
        if (!IsActive(owner))                               // force stop processing (movement can move out active zone with cleanup movegens list)
            return true;                                    // not expire now, but already lost
    }

    if (time_diff > i_travel_timer)
    {
        owner.AddSplineFlag(SPLINEFLAG_WALKMODE);

        // restore orientation of not moving creature at returning to home
        if(owner.GetDefaultMovementType()==IDLE_MOTION_TYPE)
        {
            if(CreatureData const* data = sObjectMgr.GetCreatureData(owner.GetDBTableGUIDLow()))
            {
                owner.SetOrientation(data->orientation);
                WorldPacket packet;
                owner.BuildHeartBeatMsg(&packet);
                owner.SendMessageToSet(&packet, false);
            }
        }

        owner.AI()->JustReachedHome();
        return false;
    }

    i_travel_timer -= time_diff;

    return true;
}
