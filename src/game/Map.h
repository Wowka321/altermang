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

#ifndef MANGOS_MAP_H
#define MANGOS_MAP_H

#include "Platform/Define.h"
#include "Policies/ThreadingModel.h"
#include "ace/RW_Thread_Mutex.h"
#include "ace/Thread_Mutex.h"

#include "DBCStructure.h"
#include "GridDefines.h"
#include "Cell.h"
#include "Object.h"
#include "Timer.h"
#include "SharedDefines.h"
#include "GameSystem/GridRefManager.h"
#include "MapRefManager.h"
#include "Utilities/TypeList.h"

#include <bitset>
#include <list>

class Creature;
class Unit;
class WorldPacket;
class InstanceData;
class Group;
class InstanceSave;
struct ScriptInfo;
struct ScriptAction;
class BattleGround;

//******************************************
// Map file format defines
//******************************************
struct map_fileheader
{
    uint32 mapMagic;
    uint32 versionMagic;
    uint32 buildMagic;
    uint32 areaMapOffset;
    uint32 areaMapSize;
    uint32 heightMapOffset;
    uint32 heightMapSize;
    uint32 liquidMapOffset;
    uint32 liquidMapSize;
};

#define MAP_AREA_NO_AREA      0x0001

struct map_areaHeader
{
    uint32 fourcc;
    uint16 flags;
    uint16 gridArea;
};

#define MAP_HEIGHT_NO_HEIGHT  0x0001
#define MAP_HEIGHT_AS_INT16   0x0002
#define MAP_HEIGHT_AS_INT8    0x0004

struct map_heightHeader
{
    uint32 fourcc;
    uint32 flags;
    float  gridHeight;
    float  gridMaxHeight;
};

#define MAP_LIQUID_NO_TYPE    0x0001
#define MAP_LIQUID_NO_HEIGHT  0x0002

struct map_liquidHeader
{
    uint32 fourcc;
    uint16 flags;
    uint16 liquidType;
    uint8  offsetX;
    uint8  offsetY;
    uint8  width;
    uint8  height;
    float  liquidLevel;
};

enum ZLiquidStatus
{
    LIQUID_MAP_NO_WATER     = 0x00000000,
    LIQUID_MAP_ABOVE_WATER  = 0x00000001,
    LIQUID_MAP_WATER_WALK   = 0x00000002,
    LIQUID_MAP_IN_WATER     = 0x00000004,
    LIQUID_MAP_UNDER_WATER  = 0x00000008
};

#define MAP_LIQUID_TYPE_NO_WATER    0x00
#define MAP_LIQUID_TYPE_WATER       0x01
#define MAP_LIQUID_TYPE_OCEAN       0x02
#define MAP_LIQUID_TYPE_MAGMA       0x04
#define MAP_LIQUID_TYPE_SLIME       0x08

#define MAP_ALL_LIQUIDS   (MAP_LIQUID_TYPE_WATER | MAP_LIQUID_TYPE_OCEAN | MAP_LIQUID_TYPE_MAGMA | MAP_LIQUID_TYPE_SLIME)

#define MAP_LIQUID_TYPE_DARK_WATER  0x10
#define MAP_LIQUID_TYPE_WMO_WATER   0x20

struct LiquidData
{
    uint32 type;
    float  level;
    float  depth_level;
};

class GridMap
{
    uint32  m_flags;
    // Area data
    uint16  m_gridArea;
    uint16 *m_area_map;
    // Height level data
    float   m_gridHeight;
    float   m_gridIntHeightMultiplier;
    union{
        float  *m_V9;
        uint16 *m_uint16_V9;
        uint8  *m_uint8_V9;
    };
    union{
        float  *m_V8;
        uint16 *m_uint16_V8;
        uint8  *m_uint8_V8;
    };
    // Liquid data
    uint16  m_liquidType;
    uint8   m_liquid_offX;
    uint8   m_liquid_offY;
    uint8   m_liquid_width;
    uint8   m_liquid_height;
    float   m_liquidLevel;
    uint8  *m_liquid_type;
    float  *m_liquid_map;

    bool  loadAreaData(FILE *in, uint32 offset, uint32 size);
    bool  loadHeightData(FILE *in, uint32 offset, uint32 size);
    bool  loadLiquidData(FILE *in, uint32 offset, uint32 size);

    // Get height functions and pointers
    typedef float (GridMap::*pGetHeightPtr) (float x, float y) const;
    pGetHeightPtr m_gridGetHeight;
    float  getHeightFromFloat(float x, float y) const;
    float  getHeightFromUint16(float x, float y) const;
    float  getHeightFromUint8(float x, float y) const;
    float  getHeightFromFlat(float x, float y) const;

public:
    GridMap();
    ~GridMap();
    bool  loadData(char *filaname);
    void  unloadData();

    uint16 getArea(float x, float y);
    inline float getHeight(float x, float y) {return (this->*m_gridGetHeight)(x, y);}
    float  getLiquidLevel(float x, float y);
    uint8  getTerrainType(float x, float y);
    ZLiquidStatus getLiquidStatus(float x, float y, float z, uint8 ReqLiquidType, LiquidData *data = 0);
};

// GCC have alternative #pragma pack(N) syntax and old gcc version not support pack(push,N), also any gcc version not support it at some platform
#if defined( __GNUC__ )
#pragma pack(1)
#else
#pragma pack(push,1)
#endif

struct InstanceTemplate
{
    uint32 map;                                             // instance map
    uint32 parent;                                          // non-continent parent instance (for instance with entrance in another instances)
                                                            // or 0 (not related to continent 0 map id)
    uint32 levelMin;
    uint32 levelMax;
    uint32 script_id;
};

enum LevelRequirementVsMode
{
    LEVELREQUIREMENT_HEROIC = 70
};

#if defined( __GNUC__ )
#pragma pack()
#else
#pragma pack(pop)
#endif

#define MAX_HEIGHT            100000.0f                     // can be use for find ground height at surface
#define INVALID_HEIGHT       -100000.0f                     // for check, must be equal to VMAP_INVALID_HEIGHT, real value for unknown height is VMAP_INVALID_HEIGHT_VALUE
#define MIN_UNLOAD_DELAY      1                             // immediate unload

class MANGOS_DLL_SPEC Map : public GridRefManager<NGridType>, public MaNGOS::ObjectLevelLockable<Map, ACE_Thread_Mutex>
{
    friend class MapReference;
    public:
        Map(uint32 id, time_t, uint32 InstanceId, uint8 SpawnMode, Map* _parent = NULL);
        virtual ~Map();

        // currently unused for normal maps
        bool CanUnload(uint32 diff)
        {
            if(!m_unloadTimer) return false;
            if(m_unloadTimer <= diff) return true;
            m_unloadTimer -= diff;
            return false;
        }

        virtual bool Add(Player *);
        virtual void Remove(Player *, bool);
        template<class T> void Add(T *);
        template<class T> void Remove(T *, bool);

        virtual void Update(const uint32&);

        void MessageBroadcast(Player *, WorldPacket *, bool to_self);
        void MessageBroadcast(WorldObject *, WorldPacket *);
        void MessageDistBroadcast(Player *, WorldPacket *, float dist, bool to_self, bool own_team_only = false);
        void MessageDistBroadcast(WorldObject *, WorldPacket *, float dist);

        float GetVisibilityDistance() const { return m_VisibleDistance; }
        //function for setting up visibility distance for maps on per-type/per-Id basis
        virtual void InitVisibilityDistance();

        void PlayerRelocation(Player *, float x, float y, float z, float angl);
        void CreatureRelocation(Creature *creature, float x, float y, float z, float orientation);

        template<class T, class CONTAINER> void Visit(const Cell& cell, TypeContainerVisitor<T, CONTAINER> &visitor);

        bool IsRemovalGrid(float x, float y) const
        {
            GridPair p = MaNGOS::ComputeGridPair(x, y);
            return( !getNGrid(p.x_coord, p.y_coord) || getNGrid(p.x_coord, p.y_coord)->GetGridState() == GRID_STATE_REMOVAL );
        }

        bool IsLoaded(float x, float y) const
        {
            GridPair p = MaNGOS::ComputeGridPair(x, y);
            return loaded(p);
        }

        bool GetUnloadLock(const GridPair &p) const { return getNGrid(p.x_coord, p.y_coord)->getUnloadLock(); }
        void SetUnloadLock(const GridPair &p, bool on) { getNGrid(p.x_coord, p.y_coord)->setUnloadExplicitLock(on); }
        void LoadGrid(const Cell& cell, bool no_unload = false);
        bool UnloadGrid(const uint32 &x, const uint32 &y, bool pForce);
        virtual void UnloadAll(bool pForce);

        void ResetGridExpiry(NGridType &grid, float factor = 1) const
        {
            grid.ResetTimeTracker((time_t)((float)i_gridExpiry*factor));
        }

        time_t GetGridExpiry(void) const { return i_gridExpiry; }
        uint32 GetId(void) const { return i_id; }

        static bool ExistMap(uint32 mapid, int gx, int gy);
        static bool ExistVMap(uint32 mapid, int gx, int gy);

        static void InitStateMachine();
        static void DeleteStateMachine();

        Map const * GetParent() const { return m_parentMap; }

        // some calls like isInWater should not use vmaps due to processor power
        // can return INVALID_HEIGHT if under z+2 z coord not found height
        float GetHeight(float x, float y, float z, bool pCheckVMap=true) const;
        bool IsInWater(float x, float y, float z) const;    // does not use z pos. This is for future use

        ZLiquidStatus getLiquidStatus(float x, float y, float z, uint8 ReqLiquidType, LiquidData *data = 0) const;

        uint16 GetAreaFlag(float x, float y, float z) const;
        uint8 GetTerrainType(float x, float y ) const;
        float GetWaterLevel(float x, float y ) const;
        bool IsUnderWater(float x, float y, float z) const;

        static uint32 GetAreaIdByAreaFlag(uint16 areaflag,uint32 map_id);
        static uint32 GetZoneIdByAreaFlag(uint16 areaflag,uint32 map_id);
        static void GetZoneAndAreaIdByAreaFlag(uint32& zoneid, uint32& areaid, uint16 areaflag,uint32 map_id);

        uint32 GetAreaId(float x, float y, float z) const
        {
            return GetAreaIdByAreaFlag(GetAreaFlag(x,y,z),i_id);
        }

        uint32 GetZoneId(float x, float y, float z) const
        {
            return GetZoneIdByAreaFlag(GetAreaFlag(x,y,z),i_id);
        }

        void GetZoneAndAreaId(uint32& zoneid, uint32& areaid, float x, float y, float z) const
        {
            GetZoneAndAreaIdByAreaFlag(zoneid,areaid,GetAreaFlag(x,y,z),i_id);
        }

        virtual void RemoveAllObjectsInRemoveList();

        bool CreatureRespawnRelocation(Creature *c);        // used only in CreatureRelocation and ObjectGridUnloader

        // ASSERT print helper
        bool CheckGridIntegrity(Creature* c, bool moved) const;

        uint32 GetInstanceId() const { return i_InstanceId; }
        virtual bool CanEnter(Player* /*player*/) { return true; }
        const char* GetMapName() const;

        // have meaning only for instanced map (that have set real difficulty), NOT USE its for BaseMap
        // _currently_ spawnmode == difficulty, but this can be changes later, so use appropriate spawmmode/difficult functions
        // for simplify later code support
        // regular difficulty = continent/dungeon normal/first raid normal difficulty
        uint8 GetSpawnMode() const { return (i_spawnMode); }
        Difficulty GetDifficulty() const { return Difficulty(GetSpawnMode()); }
        bool IsRegularDifficulty() const { return GetDifficulty() == REGULAR_DIFFICULTY; }
        uint32 GetMaxPlayers() const;                       // dependent from map difficulty
        uint32 GetMaxResetDelay() const;                    // dependent from map difficulty
        MapDifficulty const* GetMapDifficulty() const;      // dependent from map difficulty

        bool Instanceable() const { return i_mapEntry && i_mapEntry->Instanceable(); }
        // NOTE: this duplicate of Instanceable(), but Instanceable() can be changed when BG also will be instanceable
        bool IsDungeon() const { return i_mapEntry && i_mapEntry->IsDungeon(); }
        bool IsRaid() const { return i_mapEntry && i_mapEntry->IsRaid(); }
        bool IsRaidOrHeroicDungeon() const { return IsRaid() || GetDifficulty() > DUNGEON_DIFFICULTY_NORMAL; }
        bool IsBattleGround() const { return i_mapEntry && i_mapEntry->IsBattleGround(); }
        bool IsBattleArena() const { return i_mapEntry && i_mapEntry->IsBattleArena(); }
        bool IsBattleGroundOrArena() const { return i_mapEntry && i_mapEntry->IsBattleGroundOrArena(); }
        bool IsNextZcoordOK(float x, float y, float oldZ, float maxDiff = 5.0f) const;

        void AddObjectToRemoveList(WorldObject *obj);

        void UpdateObjectVisibility(WorldObject* obj, Cell cell, CellPair cellpair);
        void UpdateObjectsVisibilityFor(Player* player, Cell cell, CellPair cellpair);

        void resetMarkedCells() { marked_cells.reset(); }
        bool isCellMarked(uint32 pCellId) { return marked_cells.test(pCellId); }
        void markCell(uint32 pCellId) { marked_cells.set(pCellId); }

        bool HavePlayers() const { return !m_mapRefManager.isEmpty(); }
        uint32 GetPlayersCountExceptGMs() const;
        bool ActiveObjectsNearGrid(uint32 x,uint32 y) const;

        void SendToPlayers(WorldPacket const* data) const;

        typedef MapRefManager PlayerList;
        PlayerList const& GetPlayers() const { return m_mapRefManager; }

        //per-map script storage
        void ScriptsStart(std::map<uint32, std::multimap<uint32, ScriptInfo> > const& scripts, uint32 id, Object* source, Object* target);
        void ScriptCommandStart(ScriptInfo const& script, uint32 delay, Object* source, Object* target);

        // must called with AddToWorld
        template<class T>
        void AddToActive(T* obj) { AddToActiveHelper(obj); }

        void AddToActive(Creature* obj);

        // must called with RemoveFromWorld
        template<class T>
        void RemoveFromActive(T* obj) { RemoveFromActiveHelper(obj); }

        void RemoveFromActive(Creature* obj);

        Creature* GetCreature(ObjectGuid guid);
        Vehicle* GetVehicle(ObjectGuid guid);
        Pet* GetPet(ObjectGuid guid);
        Creature* GetCreatureOrPetOrVehicle(ObjectGuid guid);
        GameObject* GetGameObject(ObjectGuid guid);
        DynamicObject* GetDynamicObject(ObjectGuid guid);
        Corpse* GetCorpse(ObjectGuid guid);
        WorldObject* GetWorldObject(ObjectGuid guid);

        TypeUnorderedMapContainer<AllMapStoredObjectTypes>& GetObjectsStore() { return m_objectsStore; }

        void AddUpdateObject(Object *obj)
        {
            i_objectsToClientUpdate.insert(obj);
        }

        void RemoveUpdateObject(Object *obj)
        {
            i_objectsToClientUpdate.erase( obj );
        }

        // DynObjects currently
        uint32 GenerateLocalLowGuid(HighGuid guidhigh);
    private:
        void LoadMapAndVMap(int gx, int gy);
        void LoadVMap(int gx, int gy);
        void LoadMap(int gx,int gy, bool reload = false);
        GridMap *GetGrid(float x, float y);

        void SetTimer(uint32 t) { i_gridExpiry = t < MIN_GRID_DELAY ? MIN_GRID_DELAY : t; }

        void SendInitSelf( Player * player );

        void SendInitTransports( Player * player );
        void SendRemoveTransports( Player * player );

        void PlayerRelocationNotify(Player* player, Cell cell, CellPair cellpair);

        bool CreatureCellRelocation(Creature *creature, Cell new_cell);

        bool loaded(const GridPair &) const;
        void EnsureGridCreated(const GridPair &);
        bool EnsureGridLoaded(Cell const&);
        void EnsureGridLoadedAtEnter(Cell const&, Player* player = NULL);

        void buildNGridLinkage(NGridType* pNGridType) { pNGridType->link(this); }

        template<class T> void AddType(T *obj);
        template<class T> void RemoveType(T *obj, bool);

        NGridType* getNGrid(uint32 x, uint32 y) const
        {
            ASSERT(x < MAX_NUMBER_OF_GRIDS);
            ASSERT(y < MAX_NUMBER_OF_GRIDS);
            return i_grids[x][y];
        }

        bool isGridObjectDataLoaded(uint32 x, uint32 y) const { return getNGrid(x,y)->isGridObjectDataLoaded(); }
        void setGridObjectDataLoaded(bool pLoaded, uint32 x, uint32 y) { getNGrid(x,y)->setGridObjectDataLoaded(pLoaded); }

        void setNGrid(NGridType* grid, uint32 x, uint32 y);
        void ScriptsProcess();

        void SendObjectUpdates();
        std::set<Object *> i_objectsToClientUpdate;
    protected:
        void SetUnloadReferenceLock(const GridPair &p, bool on) { getNGrid(p.x_coord, p.y_coord)->setUnloadReferenceLock(on); }

        typedef MaNGOS::ObjectLevelLockable<Map, ACE_Thread_Mutex>::Lock Guard;

        MapEntry const* i_mapEntry;
        uint8 i_spawnMode;
        uint32 i_id;
        uint32 i_InstanceId;
        uint32 m_unloadTimer;
        float m_VisibleDistance;

        MapRefManager m_mapRefManager;
        MapRefManager::iterator m_mapRefIter;

        typedef std::set<WorldObject*> ActiveNonPlayers;
        ActiveNonPlayers m_activeNonPlayers;
        ActiveNonPlayers::iterator m_activeNonPlayersIter;
        TypeUnorderedMapContainer<AllMapStoredObjectTypes> m_objectsStore;
    private:
        time_t i_gridExpiry;

        //used for fast base_map (e.g. MapInstanced class object) search for
        //InstanceMaps and BattleGroundMaps...
        Map* m_parentMap;

        NGridType* i_grids[MAX_NUMBER_OF_GRIDS][MAX_NUMBER_OF_GRIDS];
        GridMap *GridMaps[MAX_NUMBER_OF_GRIDS][MAX_NUMBER_OF_GRIDS];
        std::bitset<TOTAL_NUMBER_OF_CELLS_PER_MAP*TOTAL_NUMBER_OF_CELLS_PER_MAP> marked_cells;

        std::set<WorldObject *> i_objectsToRemove;
        std::multimap<time_t, ScriptAction> m_scriptSchedule;

        // Map local low guid counters
        ObjectGuidGenerator<HIGHGUID_DYNAMICOBJECT> m_DynObjectGuids;
        ObjectGuidGenerator<HIGHGUID_PET> m_PetGuids;
        ObjectGuidGenerator<HIGHGUID_VEHICLE> m_VehicleGuids;

        // Type specific code for add/remove to/from grid
        template<class T>
            void AddToGrid(T*, NGridType *, Cell const&);

        template<class T>
            void AddNotifier(T*, Cell const&, CellPair const&);

        template<class T>
            void RemoveFromGrid(T*, NGridType *, Cell const&);

        template<class T>
            void DeleteFromWorld(T*);

        template<class T>
        void AddToActiveHelper(T* obj)
        {
            m_activeNonPlayers.insert(obj);
        }

        template<class T>
        void RemoveFromActiveHelper(T* obj)
        {
            // Map::Update for active object in proccess
            if(m_activeNonPlayersIter != m_activeNonPlayers.end())
            {
                ActiveNonPlayers::iterator itr = m_activeNonPlayers.find(obj);
                if(itr==m_activeNonPlayersIter)
                    ++m_activeNonPlayersIter;
                m_activeNonPlayers.erase(itr);
            }
            else
                m_activeNonPlayers.erase(obj);
        }
};

enum InstanceResetMethod
{
    INSTANCE_RESET_ALL,
    INSTANCE_RESET_CHANGE_DIFFICULTY,
    INSTANCE_RESET_GLOBAL,
    INSTANCE_RESET_GROUP_DISBAND,
    INSTANCE_RESET_GROUP_JOIN,
    INSTANCE_RESET_RESPAWN_DELAY
};

class MANGOS_DLL_SPEC InstanceMap : public Map
{
    public:
        InstanceMap(uint32 id, time_t, uint32 InstanceId, uint8 SpawnMode, Map* _parent);
        ~InstanceMap();
        bool Add(Player *);
        void Remove(Player *, bool);
        void Update(const uint32&);
        void CreateInstanceData(bool load);
        bool Reset(uint8 method);
        uint32 GetScriptId() { return i_script_id; }
        InstanceData* GetInstanceData() { return i_data; }
        void PermBindAllPlayers(Player *player);
        void UnloadAll(bool pForce);
        bool CanEnter(Player* player);
        void SendResetWarnings(uint32 timeLeft) const;
        void SetResetSchedule(bool on);

        virtual void InitVisibilityDistance();
    private:
        bool m_resetAfterUnload;
        bool m_unloadWhenEmpty;
        InstanceData* i_data;
        uint32 i_script_id;
};

class MANGOS_DLL_SPEC BattleGroundMap : public Map
{
    public:
        BattleGroundMap(uint32 id, time_t, uint32 InstanceId, Map* _parent, uint8 spawnMode);
        ~BattleGroundMap();

        void Update(const uint32&);
        bool Add(Player *);
        void Remove(Player *, bool);
        bool CanEnter(Player* player);
        void SetUnload();
        void UnloadAll(bool pForce);

        virtual void InitVisibilityDistance();
        BattleGround* GetBG() { return m_bg; }
        void SetBG(BattleGround* bg) { m_bg = bg; }
    private:
        BattleGround* m_bg;
};

/*inline
uint64
Map::CalculateGridMask(const uint32 &y) const
{
    uint64 mask = 1;
    mask <<= y;
    return mask;
}
*/

template<class T, class CONTAINER>
inline void
Map::Visit(const Cell& cell, TypeContainerVisitor<T, CONTAINER> &visitor)
{
    const uint32 x = cell.GridX();
    const uint32 y = cell.GridY();
    const uint32 cell_x = cell.CellX();
    const uint32 cell_y = cell.CellY();

    if( !cell.NoCreate() || loaded(GridPair(x,y)) )
    {
        EnsureGridLoaded(cell);
        getNGrid(x, y)->Visit(cell_x, cell_y, visitor);
    }
}
#endif
