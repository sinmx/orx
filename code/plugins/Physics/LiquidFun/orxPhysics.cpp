/* Orx - Portable Game Engine
 *
 * Copyright (c) 2008-2015 Orx-Project
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 *    1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 *
 *    2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 *
 *    3. This notice may not be removed or altered from any source
 *    distribution.
 */

/**
 * @file orxPhysics.cpp
 * @date 24/03/2008
 * @author iarwain@orx-project.org
 *
 * Box2D physics plugin implementation
 *
 */


#include "orxPluginAPI.h"

#include <Box2D/Box2D.h>
#include <stdlib.h>


#ifdef __orxMSVC__

  #pragma warning(disable : 4311 4312)

#endif /* __orxMSVC__ */


#if defined(__orxWINDOWS__) && defined(__orxGCC__)

  #define alloca __builtin_alloca

#endif /* __orxWINDOWS__ && __orxGCC__ */

#if defined(__orxDEBUG__) || defined(__orxPROFILER__)

  #define orxPHYSICS_ENABLE_DEBUG_DRAW

#endif /* __orxDEBUG__ || __orxPROFILER__ */


/** Module flags
 */
#define orxPHYSICS_KU32_STATIC_FLAG_NONE        0x00000000 /**< No flags */

#define orxPHYSICS_KU32_STATIC_FLAG_READY       0x00000001 /**< Ready flag */

#define orxPHYSICS_KU32_STATIC_FLAG_ENABLED     0x00000002 /**< Enabled flag */

#define orxPHYSICS_KU32_STATIC_MASK_ALL         0xFFFFFFFF /**< All mask */

namespace orxPhysics
{
  static const orxU32   su32DefaultIterations   = 10;
  static const orxFLOAT sfDefaultDimensionRatio = orx2F(0.01f);
  static const orxU32   su32MessageBankSize     = 512;
  static const orxFLOAT sfMaxDT                 = orx2F(1.0f / 30.0f);
  static const orxU32   su32MaxSteps            = 5;
  static const orxFLOAT sfSmallesParticleRadius = orx2F(0.04f);
}


/***************************************************************************
 * Structure declaration                                                   *
 ***************************************************************************/

/** Event storage
 */
typedef struct __orxPHYSICS_EVENT_STORAGE_t
{
  orxLINKLIST_NODE                  stNode;           /**< Link list node */
  b2Body                           *poSource;         /**< Event source */
  b2Body                           *poDestination;    /**< Event destination */
  orxPHYSICS_EVENT                  eID;              /**< Event ID */
  orxPHYSICS_EVENT_PAYLOAD          stPayload;        /**< Event payload */

} orxPHYSICS_EVENT_STORAGE;

/** Contact listener
 */
class orxPhysicsContactListener : public b2ContactListener
{
public:
  void BeginContact(b2Contact *_poContact);
  void EndContact(b2Contact *_poContact);
};


#ifdef orxPHYSICS_ENABLE_DEBUG_DRAW

/** Debug draw
 */
class orxPhysicsDebugDraw : public b2Draw
{
public:
  void DrawPolygon(const b2Vec2 *_avVertexList, int32 _s32VertexNumber, const b2Color &_rstColor);
  void DrawSolidPolygon(const b2Vec2 *_avVertexList, int32 _s32VertexNumber, const b2Color &_rstColor);
  void DrawCircle(const b2Vec2 &_rvCenter, float32 _fRadius, const b2Color &_rstColor);
  void DrawSolidCircle(const b2Vec2 &_rvCenter, float32 _fRadius, const b2Vec2 &_rvAxis, const b2Color &_rstColor);
  void DrawSegment(const b2Vec2 &_rvP1, const b2Vec2 &_rvP2, const b2Color &_rstColor);
  void DrawTransform(const b2Transform &_rstTransform);
  void DrawParticles(const b2Vec2 *_avCenterList, float32 _fRadius, const b2ParticleColor *_astColorList, int32 _s32Count);
};

#endif /* orxPHYSICS_ENABLE_DEBUG_DRAW */


/** Static structure
 */
typedef struct __orxPHYSICS_STATIC_t
{
  orxU32                      u32Flags;              /**< Control flags */
  orxU32                      u32Iterations;         /**< Simulation iterations per step */
  orxU32                      u32ParticleIterations; /**< Particule iterations per step */
  orxFLOAT                    fDimensionRatio;       /**< Dimension ratio */
  orxFLOAT                    fRecDimensionRatio;    /**< Reciprocal dimension ratio */
  orxFLOAT                    fLastDT;               /**< Last DT */
  orxLINKLIST                 stEventList;           /**< Event link list */
  orxBANK                    *pstEventBank;          /**< Event bank */
  b2World                    *poWorld;               /**< World */
  orxPhysicsContactListener  *poContactListener;     /**< Contact listener */
  orxHASHTABLE               *pstParticleSystems;    /**< Particle System Hashtable*/

  orxFLOAT                    fFixedTimeStep;        /**< Fixed timestep */
  orxFLOAT                    fFixedTimestepAccumulatorRatio;
  orxFLOAT                    fFixedTimestepAccumulator;

#ifdef orxPHYSICS_ENABLE_DEBUG_DRAW

  orxPhysicsDebugDraw        *poDebugDraw;        /**< Debug draw */

#endif /* orxPHYSICS_ENABLE_DEBUG_DRAW */

} orxPHYSICS_STATIC;


/***************************************************************************
 * Static variables                                                        *
 ***************************************************************************/

/** Static data
 */
static orxPHYSICS_STATIC sstPhysics;


/***************************************************************************
 * Private functions                                                       *
 ***************************************************************************/

void *orxPhysics_LiquidFun_Allocate(int32 _iSize, void* callbackData)
{
  return orxMemory_Allocate((orxU32)_iSize, orxMEMORY_TYPE_PHYSICS);
}

void orxPhysics_LiquidFun_Free(void *_pMem, void* callbackData)
{
  orxMemory_Free(_pMem);
}

class RayCastCallback : public b2RayCastCallback
{
public:

  RayCastCallback() : hResult(orxHANDLE_UNDEFINED), bEarlyExit(orxFALSE), u16SelfFlags(0), u16CheckMask(0)
  {
  }

  float32 ReportFixture(b2Fixture *_poFixture, const b2Vec2 &_rvContact, const b2Vec2 &_rvNormal, float32 _fFraction)
  {
    float32 fResult;

    /* Has hit? */
    if(_poFixture)
    {
      /* Gets fixture's filter info */
      const b2Filter &rstFilter = _poFixture->GetFilterData();

      /* Match? */
      if(((rstFilter.maskBits & u16SelfFlags) != 0)
      && ((rstFilter.categoryBits & u16CheckMask) != 0))
      {
        /* Stores contact and normal */
        orxVector_Set(&vContact, sstPhysics.fRecDimensionRatio * _rvContact.x, sstPhysics.fRecDimensionRatio * _rvContact.y, orxFLOAT_0);
        orxVector_Set(&vNormal, _rvNormal.x, _rvNormal.y, orxFLOAT_0);

        /* Stores associated object's handle */
        hResult = (orxHANDLE)_poFixture->GetBody()->GetUserData();

        /* Early exit? */
        if(bEarlyExit != orxFALSE)
        {
          /* Stops now */
          fResult = 0.0f;
        }
        else
        {
          /* Checks for closer fixture */
          fResult = _fFraction;
        }
      }
      else
      {
        /* Ignores fixture completely */
        fResult = -1.0f;
      }
    }
    else
    {
      /* Stops now */
      fResult = 0.0f;
    }

    /* Done! */
    return fResult;
  }

  orxVECTOR   vContact;
  orxVECTOR   vNormal;
  orxHANDLE   hResult;
  orxBOOL     bEarlyExit;
  orxU16      u16SelfFlags;
  orxU16      u16CheckMask;
};

static void orxFASTCALL orxPhysics_LiquidFun_SendContactEvent(b2Contact *_poContact, orxPHYSICS_EVENT _eEventID)
{
  orxBODY_PART             *pstSourceBodyPart, *pstDestinationBodyPart;

  /* Gets body parts */
  pstSourceBodyPart       = (orxBODY_PART *)_poContact->GetFixtureA()->GetUserData();
  pstDestinationBodyPart  = (orxBODY_PART *)_poContact->GetFixtureB()->GetUserData();

  /* Valid? */
  if((pstSourceBodyPart != orxNULL) && (pstDestinationBodyPart != orxNULL))
  {
    orxPHYSICS_EVENT_STORAGE *pstEventStorage;

    /* Adds a contact event */
    pstEventStorage = (orxPHYSICS_EVENT_STORAGE *)orxBank_Allocate(sstPhysics.pstEventBank);

    /* Valid? */
    if(pstEventStorage != orxNULL)
    {
      b2Body *poSource, *poDestination;

      /* Adds it to list */
      orxLinkList_AddEnd(&(sstPhysics.stEventList), &(pstEventStorage->stNode));

      /* Gets both bodies */
      poSource      = _poContact->GetFixtureA()->GetBody();
      poDestination = _poContact->GetFixtureB()->GetBody();

      /* Inits it */
      pstEventStorage->eID            = _eEventID;
      pstEventStorage->poSource       = poSource;
      pstEventStorage->poDestination  = poDestination;

      /* Contact add? */
      if(_eEventID == orxPHYSICS_EVENT_CONTACT_ADD)
      {
        const b2Manifold *poManifold;

        /* Gets local manifold */
        poManifold = _poContact->GetManifold();

        /* 2 contacts? */
        if(poManifold->pointCount > 1)
        {
          b2WorldManifold oManifold;

          /* Gets global manifold */
          _poContact->GetWorldManifold(&oManifold);

          /* Updates values */
          orxVector_Set(&(pstEventStorage->stPayload.vPosition), orx2F(0.5f) * sstPhysics.fRecDimensionRatio * (oManifold.points[0].x + oManifold.points[1].x), orx2F(0.5f) * sstPhysics.fRecDimensionRatio * (oManifold.points[0].y + oManifold.points[1].y), orxFLOAT_0);
          orxVector_Set(&(pstEventStorage->stPayload.vNormal), oManifold.normal.x, oManifold.normal.y, orxFLOAT_0);
        }
        /* 1 contact? */
        else if(poManifold->pointCount == 1)
        {
          b2WorldManifold oManifold;

          /* Gets global manifold */
          _poContact->GetWorldManifold(&oManifold);

          /* Updates values */
          orxVector_Set(&(pstEventStorage->stPayload.vPosition), sstPhysics.fRecDimensionRatio * oManifold.points[0].x, sstPhysics.fRecDimensionRatio * oManifold.points[0].y, orxFLOAT_0);
          orxVector_Set(&(pstEventStorage->stPayload.vNormal), oManifold.normal.x, oManifold.normal.y, orxFLOAT_0);
        }
        /* 0 contact */
        else
        {
          orxVector_Copy(&(pstEventStorage->stPayload.vPosition), &orxVECTOR_0);
          orxVector_Copy(&(pstEventStorage->stPayload.vNormal), &orxVECTOR_0);
        }
      }
      else
      {
        orxVector_Copy(&(pstEventStorage->stPayload.vPosition), &orxVECTOR_0);
        orxVector_Copy(&(pstEventStorage->stPayload.vNormal), &orxVECTOR_0);
      }

      /* Updates part names */
      pstEventStorage->stPayload.zSenderPartName    = orxBody_GetPartName(pstSourceBodyPart);
      pstEventStorage->stPayload.zRecipientPartName = orxBody_GetPartName(pstDestinationBodyPart);
    }
  }

  return;
}

void orxPhysicsContactListener::BeginContact(b2Contact *_poContact)
{
  /* Sends contact event */
  orxPhysics_LiquidFun_SendContactEvent(_poContact, orxPHYSICS_EVENT_CONTACT_ADD);

  return;
}

void orxPhysicsContactListener::EndContact(b2Contact *_poContact)
{
  /* Sends contact event */
  orxPhysics_LiquidFun_SendContactEvent(_poContact, orxPHYSICS_EVENT_CONTACT_REMOVE);

  return;
}


#ifdef orxPHYSICS_ENABLE_DEBUG_DRAW

void orxPhysicsDebugDraw::DrawPolygon(const b2Vec2 *_avVertexList, int32 _s32VertexNumber, const b2Color &_rstColor)
{
  orxVIEWPORT  *pstViewport;
  orxCOLOR      stColor;
  orxS32        i;

#ifndef __orxMSVC__

  orxVECTOR avVertexList[_s32VertexNumber];

#else /* __orxMSVC__ */

  orxVECTOR *avVertexList = (orxVECTOR *)_malloca(_s32VertexNumber * sizeof(orxVECTOR));

#endif /* __orxMSVC__ */

  /* For all viewports */
  for(pstViewport = orxVIEWPORT(orxStructure_GetFirst(orxSTRUCTURE_ID_VIEWPORT));
      pstViewport != orxNULL;
      pstViewport = orxVIEWPORT(orxStructure_GetNext(pstViewport)))
  {
    orxCAMERA *pstCamera;

    /* Gets viewport camera */
    pstCamera = orxViewport_GetCamera(pstViewport);

    /* Valid? */
    if(pstCamera != orxNULL)
    {
      orxAABOX  stFrustum;
      orxVECTOR vCameraPosition;
      orxFLOAT  fZ = orxFLOAT_0;

      /* Gets camera position */
      orxFrame_GetPosition(orxCamera_GetFrame(pstCamera), orxFRAME_SPACE_GLOBAL, &vCameraPosition);

      /* Gets its frustum */
      orxCamera_GetFrustum(pstCamera, &stFrustum);

      /* Stores it bottom Z */
      fZ = stFrustum.vBR.fZ + vCameraPosition.fZ;

      /* For all vertices */
      for(i = 0; i < _s32VertexNumber; i++)
      {
        orxVECTOR vTemp;

        /* Sets it */
        orxVector_Set(&vTemp, sstPhysics.fRecDimensionRatio * orx2F(_avVertexList[i].x), sstPhysics.fRecDimensionRatio * orx2F(_avVertexList[i].y), fZ);

        /* Stores its screen position */
        orxRender_GetScreenPosition(&vTemp, pstViewport, &(avVertexList[i]));
      }

      /* Sets color */
      orxVector_Set(&(stColor.vRGB), orx2F(_rstColor.r), orx2F(_rstColor.g), orx2F(_rstColor.b));
      stColor.fAlpha = orxFLOAT_1;

      /* Draws polygon */
      orxDisplay_DrawPolygon(avVertexList, (orxS32)_s32VertexNumber, orxColor_ToRGBA(&stColor), orxFALSE);
    }
  }

  /* Done! */
  return;
}

void orxPhysicsDebugDraw::DrawSolidPolygon(const b2Vec2 *_avVertexList, int32 _s32VertexNumber, const b2Color &_rstColor)
{
  orxVIEWPORT  *pstViewport;
  orxCOLOR      stColor;
  orxS32        i;

#ifndef __orxMSVC__

  orxVECTOR avVertexList[_s32VertexNumber];

#else /* __orxMSVC__ */

  orxVECTOR *avVertexList = (orxVECTOR *)_malloca(_s32VertexNumber * sizeof(orxVECTOR));

#endif /* __orxMSVC__ */

  /* For all viewports */
  for(pstViewport = orxVIEWPORT(orxStructure_GetFirst(orxSTRUCTURE_ID_VIEWPORT));
      pstViewport != orxNULL;
      pstViewport = orxVIEWPORT(orxStructure_GetNext(pstViewport)))
  {
    orxCAMERA *pstCamera;

    /* Gets viewport camera */
    pstCamera = orxViewport_GetCamera(pstViewport);

    /* Valid? */
    if(pstCamera != orxNULL)
    {
      orxAABOX  stFrustum;
      orxVECTOR vCameraPosition;
      orxFLOAT  fZ = orxFLOAT_0;

      /* Gets camera position */
      orxFrame_GetPosition(orxCamera_GetFrame(pstCamera), orxFRAME_SPACE_GLOBAL, &vCameraPosition);

      /* Gets its frustum */
      orxCamera_GetFrustum(pstCamera, &stFrustum);

      /* Stores it bottom Z */
      fZ = stFrustum.vBR.fZ + vCameraPosition.fZ;

      /* For all vertices */
      for(i = 0; i < _s32VertexNumber; i++)
      {
        orxVECTOR vTemp;

        /* Sets it */
        orxVector_Set(&vTemp, sstPhysics.fRecDimensionRatio * orx2F(_avVertexList[i].x), sstPhysics.fRecDimensionRatio * orx2F(_avVertexList[i].y), fZ);

        /* Stores its screen position */
        orxRender_GetScreenPosition(&vTemp, pstViewport, &(avVertexList[i]));
      }

      /* Sets color */
      orxVector_Set(&(stColor.vRGB), orx2F(_rstColor.r), orx2F(_rstColor.g), orx2F(_rstColor.b));

      /* Draws polygon inside */
      stColor.fAlpha = orx2F(0.5f);
      orxDisplay_DrawPolygon(avVertexList, (orxS32)_s32VertexNumber, orxColor_ToRGBA(&stColor), orxTRUE);

      /* Draws polygon outside */
      stColor.fAlpha = orxFLOAT_1;
      orxDisplay_DrawPolygon(avVertexList, (orxS32)_s32VertexNumber, orxColor_ToRGBA(&stColor), orxFALSE);
    }
  }

  /* Done! */
  return;
}

void orxPhysicsDebugDraw::DrawCircle(const b2Vec2 &_rvCenter, float32 _fRadius, const b2Color &_rstColor)
{
  orxVIEWPORT  *pstViewport;
  orxCOLOR      stColor;
  orxVECTOR     vCenter, vTemp;

  /* For all viewports */
  for(pstViewport = orxVIEWPORT(orxStructure_GetFirst(orxSTRUCTURE_ID_VIEWPORT));
      pstViewport != orxNULL;
      pstViewport = orxVIEWPORT(orxStructure_GetNext(pstViewport)))
  {
    orxCAMERA *pstCamera;

    /* Gets viewport camera */
    pstCamera = orxViewport_GetCamera(pstViewport);

    /* Valid? */
    if(pstCamera != orxNULL)
    {
      orxAABOX  stFrustum;
      orxVECTOR vCameraPosition;
      orxFLOAT  fZ = orxFLOAT_0;

      /* Gets camera position */
      orxFrame_GetPosition(orxCamera_GetFrame(pstCamera), orxFRAME_SPACE_GLOBAL, &vCameraPosition);

      /* Gets its frustum */
      orxCamera_GetFrustum(pstCamera, &stFrustum);

      /* Stores it bottom Z */
      fZ = stFrustum.vBR.fZ + vCameraPosition.fZ;

      /* Inits center & temp vectors */
      orxVector_Set(&vCenter, sstPhysics.fRecDimensionRatio * orx2F(_rvCenter.x), sstPhysics.fRecDimensionRatio * orx2F(_rvCenter.y), fZ);
      orxVector_Copy(&vTemp, &vCenter);
      vTemp.fX += sstPhysics.fRecDimensionRatio * orx2F(_fRadius);

      /* Gets its screen position */
      orxRender_GetScreenPosition(&vCenter, pstViewport, &vCenter);
      orxRender_GetScreenPosition(&vTemp, pstViewport, &vTemp);

      /* Sets color */
      orxVector_Set(&(stColor.vRGB), orx2F(_rstColor.r), orx2F(_rstColor.g), orx2F(_rstColor.b));
      stColor.fAlpha = orxFLOAT_1;

      /* Draws circle */
      orxDisplay_DrawCircle(&vCenter, vTemp.fX - vCenter.fX, orxColor_ToRGBA(&stColor), orxFALSE);
    }
  }

  /* Done! */
  return;
}

void orxPhysicsDebugDraw::DrawSolidCircle(const b2Vec2 &_rvCenter, float32 _fRadius, const b2Vec2 &_rvAxis, const b2Color &_rstColor)
{
  orxVIEWPORT  *pstViewport;
  orxCOLOR      stColor;
  orxVECTOR     vCenter, vTemp;

  /* For all viewports */
  for(pstViewport = orxVIEWPORT(orxStructure_GetFirst(orxSTRUCTURE_ID_VIEWPORT));
      pstViewport != orxNULL;
      pstViewport = orxVIEWPORT(orxStructure_GetNext(pstViewport)))
  {
    orxCAMERA *pstCamera;

    /* Gets viewport camera */
    pstCamera = orxViewport_GetCamera(pstViewport);

    /* Valid? */
    if(pstCamera != orxNULL)
    {
      orxAABOX  stFrustum;
      orxVECTOR vCameraPosition;
      orxFLOAT  fZ = orxFLOAT_0;

      /* Gets camera position */
      orxFrame_GetPosition(orxCamera_GetFrame(pstCamera), orxFRAME_SPACE_GLOBAL, &vCameraPosition);

      /* Gets its frustum */
      orxCamera_GetFrustum(pstCamera, &stFrustum);

      /* Stores it bottom Z */
      fZ = stFrustum.vBR.fZ + vCameraPosition.fZ;

      /* Inits center & temp vectors */
      orxVector_Set(&vCenter, sstPhysics.fRecDimensionRatio * orx2F(_rvCenter.x), sstPhysics.fRecDimensionRatio * orx2F(_rvCenter.y), fZ);
      orxVector_Copy(&vTemp, &vCenter);
      vTemp.fX += sstPhysics.fRecDimensionRatio * orx2F(_fRadius);

      /* Gets their screen position */
      orxRender_GetScreenPosition(&vCenter, pstViewport, &vCenter);
      orxRender_GetScreenPosition(&vTemp, pstViewport, &vTemp);

      /* Sets color */
      orxVector_Set(&(stColor.vRGB), orx2F(_rstColor.r), orx2F(_rstColor.g), orx2F(_rstColor.b));

      /* Draws circle inside */
      stColor.fAlpha = orx2F(0.5f);
      orxDisplay_DrawCircle(&vCenter, vTemp.fX - vCenter.fX, orxColor_ToRGBA(&stColor), orxTRUE);

      /* Draws circle outside */
      stColor.fAlpha = orxFLOAT_1;
      orxDisplay_DrawCircle(&vCenter, vTemp.fX - vCenter.fX, orxColor_ToRGBA(&stColor), orxFALSE);
    }
  }

  /* Done! */
  return;
}

void orxPhysicsDebugDraw::DrawSegment(const b2Vec2 &_rvP1, const b2Vec2 &_rvP2, const b2Color &_rstColor)
{
  orxVIEWPORT  *pstViewport;
  orxCOLOR      stColor;
  orxVECTOR     vStart, vEnd;

  /* For all viewports */
  for(pstViewport = orxVIEWPORT(orxStructure_GetFirst(orxSTRUCTURE_ID_VIEWPORT));
      pstViewport != orxNULL;
      pstViewport = orxVIEWPORT(orxStructure_GetNext(pstViewport)))
  {
    orxCAMERA *pstCamera;

    /* Gets viewport camera */
    pstCamera = orxViewport_GetCamera(pstViewport);

    /* Valid? */
    if(pstCamera != orxNULL)
    {
      orxAABOX  stFrustum;
      orxVECTOR vCameraPosition;
      orxFLOAT  fZ = orxFLOAT_0;

      /* Gets camera position */
      orxFrame_GetPosition(orxCamera_GetFrame(pstCamera), orxFRAME_SPACE_GLOBAL, &vCameraPosition);

      /* Gets its frustum */
      orxCamera_GetFrustum(pstCamera, &stFrustum);

      /* Stores it bottom Z */
      fZ = stFrustum.vBR.fZ + vCameraPosition.fZ;

      /* Inits points */
      orxVector_Set(&vStart, sstPhysics.fRecDimensionRatio * orx2F(_rvP1.x), sstPhysics.fRecDimensionRatio * orx2F(_rvP1.y), fZ);
      orxVector_Set(&vEnd, sstPhysics.fRecDimensionRatio * orx2F(_rvP2.x), sstPhysics.fRecDimensionRatio * orx2F(_rvP2.y), fZ);

      /* Gets their screen positions */
      orxRender_GetScreenPosition(&vStart, pstViewport, &vStart);
      orxRender_GetScreenPosition(&vEnd, pstViewport, &vEnd);

      /* Sets color */
      orxVector_Set(&(stColor.vRGB), orx2F(_rstColor.r), orx2F(_rstColor.g), orx2F(_rstColor.b));
      stColor.fAlpha = orxFLOAT_1;

      /* Draws segment */
      orxDisplay_DrawLine(&vStart, &vEnd, orxColor_ToRGBA(&stColor));
    }
  }

  /* Done! */
  return;
}

void orxPhysicsDebugDraw::DrawParticles(const b2Vec2 *_avCenterList, float32 _fRadius, const b2ParticleColor *_astColorList, int32 _s32Count)
{
  b2Color color(1, 1, 1);

  for(orxS32 i = 0; i < _s32Count; i++)
  {
    DrawCircle(_avCenterList[i], _fRadius, color);
  }

  /* Done! */
  return;
}

void orxPhysicsDebugDraw::DrawTransform(const b2Transform &_rstTransform)
{
  orxVIEWPORT    *pstViewport;
  orxVECTOR       vStart, vEndX, vEndY;
  const orxFLOAT  fScale = orx2F(0.4f);

  /* For all viewports */
  for(pstViewport = orxVIEWPORT(orxStructure_GetFirst(orxSTRUCTURE_ID_VIEWPORT));
      pstViewport != orxNULL;
      pstViewport = orxVIEWPORT(orxStructure_GetNext(pstViewport)))
  {
    orxCAMERA *pstCamera;

    /* Gets viewport camera */
    pstCamera = orxViewport_GetCamera(pstViewport);

    /* Valid? */
    if(pstCamera != orxNULL)
    {
      orxAABOX  stFrustum;
      orxVECTOR vCameraPosition;
      orxFLOAT  fZ = orxFLOAT_0;

      /* Gets camera position */
      orxFrame_GetPosition(orxCamera_GetFrame(pstCamera), orxFRAME_SPACE_GLOBAL, &vCameraPosition);

      /* Gets its frustum */
      orxCamera_GetFrustum(pstCamera, &stFrustum);

      /* Stores it bottom Z */
      fZ = stFrustum.vBR.fZ + vCameraPosition.fZ;

      /* Inits points */
      orxVector_Set(&vStart, sstPhysics.fRecDimensionRatio * orx2F(_rstTransform.p.x), sstPhysics.fRecDimensionRatio * orx2F(_rstTransform.p.y), fZ);
      orxVector_Set(&vEndX, vStart.fX + sstPhysics.fRecDimensionRatio * fScale * orx2F(_rstTransform.q.c), vStart.fY + sstPhysics.fRecDimensionRatio * fScale * orx2F(_rstTransform.q.s), fZ);
      orxVector_Set(&vEndY, vStart.fX + sstPhysics.fRecDimensionRatio * fScale * orx2F(-_rstTransform.q.s), vStart.fY + sstPhysics.fRecDimensionRatio * fScale * orx2F(_rstTransform.q.c), fZ);

      /* Gets their screen positions */
      orxRender_GetScreenPosition(&vStart, pstViewport, &vStart);
      orxRender_GetScreenPosition(&vEndX, pstViewport, &vEndX);
      orxRender_GetScreenPosition(&vEndY, pstViewport, &vEndY);

      /* Draws segments */
      orxDisplay_DrawLine(&vStart, &vEndX, orx2RGBA(0xFF, 0x00, 0x00, 0xFF));
      orxDisplay_DrawLine(&vStart, &vEndY, orx2RGBA(0x00, 0xFF, 0x00, 0xFF));
    }
  }

  /* Done! */
  return;
}

static orxSTATUS orxFASTCALL orxPhysics_LiquidFun_EventHandler(const orxEVENT *_pstEvent)
{
  orxSTATUS eResult = orxSTATUS_SUCCESS;

  /* Checks */
  orxASSERT(_pstEvent->eType == orxEVENT_TYPE_RENDER);

  /* End of rendering? */
  if(_pstEvent->eID == orxRENDER_EVENT_STOP)
  {
    /* Pushes config section */
    orxConfig_PushSection(orxPHYSICS_KZ_CONFIG_SECTION);

    /* Show debug? */
    if(orxConfig_GetBool(orxPHYSICS_KZ_CONFIG_SHOW_DEBUG) != orxFALSE)
    {
      /* Draws debug */
      sstPhysics.poWorld->DrawDebugData();
    }

    /* Pops config section */
    orxConfig_PopSection();
  }

  /* Done! */
  return eResult;
}

#endif /* orxPHYSICS_ENABLE_DEBUG_DRAW */

/** Applies physics simulation result to the body
 * @param[in]   _pstBody                      Concerned body
 */
static void orxFASTCALL orxPhysics_ApplySimulationResult(orxPHYSICS_BODY *_pstBody)
{
  b2Body         *poBody;
  orxOBJECT      *pstObject;
  orxBODY        *pstBody;
  orxFRAME       *pstFrame;
  orxFRAME_SPACE  eFrameSpace;

  /* Profiles */
  orxPROFILER_PUSH_MARKER("orxPhysics_ApplySimResult");

  /* Gets physics body */
  poBody = (b2Body *)_pstBody;

  /* Gets associated body */
  pstBody = orxBODY(poBody->GetUserData());

  /* Gets owner object */
  pstObject = orxOBJECT(orxStructure_GetOwner(pstBody));

  /* Gets its frame */
  pstFrame = orxOBJECT_GET_STRUCTURE(pstObject, FRAME);

  /* Gets its frame space */
  eFrameSpace = (orxFrame_IsRootChild(pstFrame) != orxFALSE) ? orxFRAME_SPACE_LOCAL : orxFRAME_SPACE_GLOBAL;

  /* Is enabled? */
  if(orxObject_IsEnabled(pstObject) != orxFALSE)
  {
    orxVECTOR   vSpeed, vOldPos, vNewPos;
    orxCLOCK   *pstClock;
    orxFLOAT    fCoef = orxFLOAT_1;

    /* Gets its clock */
    pstClock = orxObject_GetClock(pstObject);

    /* Valid */
    if(pstClock != orxNULL)
    {
      const orxCLOCK_INFO *pstClockInfo;

      /* Gets its info */
      pstClockInfo = orxClock_GetInfo(pstClock);

      /* Has multiplier? */
      if(pstClockInfo->eModType == orxCLOCK_MOD_TYPE_MULTIPLY)
      {
        /* Uses it */
        fCoef = (pstClockInfo->fModValue != orxFLOAT_0) ? orxFLOAT_1 / pstClockInfo->fModValue : orxFLOAT_0;
      }
    }

    /* Gets resulting speed */
    orxPhysics_GetSpeed(_pstBody, &vSpeed);

    /* Global space? */
    if(eFrameSpace == orxFRAME_SPACE_GLOBAL)
    {
      orxVECTOR vScale;
      orxFRAME *pstParentFrame;

      /* Gets parent frame */
      pstParentFrame = orxFRAME(orxStructure_GetParent(pstFrame));

      /* Updates speed according to parent scale & rotation */
      orxVector_2DRotate(&vSpeed, &vSpeed, -orxFrame_GetRotation(pstParentFrame, orxFRAME_SPACE_GLOBAL));
      orxVector_Div(&vSpeed, &vSpeed, orxFrame_GetScale(pstParentFrame, orxFRAME_SPACE_GLOBAL, &vScale));
    }

    /* Updates its speed & angular velocity */
    orxBody_SetSpeed(pstBody, orxVector_Mulf(&vSpeed, &vSpeed, fCoef));
    orxBody_SetAngularVelocity(pstBody, fCoef * orxPhysics_GetAngularVelocity(_pstBody));

    /* Updates rotation */
    orxFrame_SetRotation(pstFrame, eFrameSpace, orxPhysics_GetRotation(_pstBody));

    /* Updates position */
    orxFrame_GetPosition(pstFrame, eFrameSpace, &vOldPos);
    orxPhysics_GetPosition(_pstBody, &vNewPos);
    vNewPos.fZ = vOldPos.fZ;
    orxFrame_SetPosition(pstFrame, eFrameSpace, &vNewPos);
  }
  else
  {
    orxVECTOR vPosition;

    /* Enforces its body properties */
    orxPhysics_SetRotation(_pstBody, orxFrame_GetRotation(pstFrame, eFrameSpace));
    orxPhysics_SetAngularVelocity(_pstBody, orxFLOAT_0);
    orxPhysics_SetPosition(_pstBody, orxFrame_GetPosition(pstFrame, eFrameSpace, &vPosition));
    orxPhysics_SetSpeed(_pstBody, &orxVECTOR_0);
  }

  /* Profiles */
  orxPROFILER_POP_MARKER();

  /* Done! */
  return;
}

/** Update (callback to register on a clock)
 * @param[in]   _pstClockInfo   Clock info of the clock used upon registration
 * @param[in]   _pContext       Context sent when registering callback to the clock
 */
static void orxFASTCALL orxPhysics_LiquidFun_Update(const orxCLOCK_INFO *_pstClockInfo, void *_pContext)
{
  orxPHYSICS_EVENT_STORAGE *pstEventStorage;
  b2Body                   *poBody;

  /* Profiles */
  orxPROFILER_PUSH_MARKER("orxPhysics_Update");

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstClockInfo != orxNULL);

  /* For all physical bodies */
  for(poBody = sstPhysics.poWorld->GetBodyList();
      poBody != NULL;
      poBody = poBody->GetNext())
  {
    orxOBJECT  *pstObject;
    orxBODY    *pstBody;

    /* Gets associated body */
    pstBody = orxBODY(poBody->GetUserData());

    /* Gets owner object */
    pstObject = orxOBJECT(orxStructure_GetOwner(pstBody));

    /* Is enabled? */
    if(orxObject_IsEnabled(pstObject) != orxFALSE)
    {
      orxFRAME   *pstFrame;
      orxVECTOR   vSpeed, vGravity;
      orxCLOCK   *pstClock;
      orxFLOAT    fCoef = orxFLOAT_1;

      /* Gets its clock */
      pstClock = orxObject_GetClock(pstObject);

      /* Valid */
      if(pstClock != orxNULL)
      {
        const orxCLOCK_INFO *pstClockInfo;

        /* Gets its info */
        pstClockInfo = orxClock_GetInfo(pstClock);

        /* Has multiplier? */
        if(pstClockInfo->eModType == orxCLOCK_MOD_TYPE_MULTIPLY)
        {
          /* Uses it */
          fCoef = pstClockInfo->fModValue;
        }
      }

      /* Enforces its activation state */
      poBody->SetActive(true);

      /* Gets owner's frame */
      pstFrame = orxOBJECT_GET_STRUCTURE(pstObject, FRAME);

      /* Gets its body speed */
      orxBody_GetSpeed(pstBody, &vSpeed);

      /* Is not a root child? */
      if(orxFrame_IsRootChild(pstFrame) == orxFALSE)
      {
        orxVECTOR vPos, vScale;
        orxFRAME *pstParentFrame;

        /* Updates body position & rotation */
        orxPhysics_SetPosition((orxPHYSICS_BODY *)poBody, orxFrame_GetPosition(pstFrame, orxFRAME_SPACE_GLOBAL, &vPos));
        orxPhysics_SetRotation((orxPHYSICS_BODY *)poBody, orxFrame_GetRotation(pstFrame, orxFRAME_SPACE_GLOBAL));

        /* Gets parent frame */
        pstParentFrame = orxFRAME(orxStructure_GetParent(pstFrame));

        /* Updates speed according to parent scale & rotation */
        orxVector_2DRotate(&vSpeed, &vSpeed, orxFrame_GetRotation(pstParentFrame, orxFRAME_SPACE_GLOBAL));
        orxVector_Mul(&vSpeed, &vSpeed, orxFrame_GetScale(pstParentFrame, orxFRAME_SPACE_GLOBAL, &vScale));
      }

      /* Applies speed & angular velocity */
      orxPhysics_SetSpeed((orxPHYSICS_BODY *)poBody, orxVector_Mulf(&vSpeed, &vSpeed, fCoef));
      orxPhysics_SetAngularVelocity((orxPHYSICS_BODY *)poBody, fCoef * orxBody_GetAngularVelocity(pstBody));

      /* No custom gravity */
      if(orxBody_GetCustomGravity(pstBody, &vGravity) == orxNULL)
      {
        /* Uses world gravity */
        orxPhysics_GetGravity(&vGravity);
      }

      /* Applies modified gravity */
      orxPhysics_SetCustomGravity((orxPHYSICS_BODY *)poBody, orxVector_Mulf(&vGravity, &vGravity, fCoef * fCoef));
    }
    else
    {
      /* Is still active? */
      if(poBody->IsActive() != false)
      {
        /* Deactivates it */
        poBody->SetActive(false);
      }
    }
  }

  /* Is simulation enabled? */
  if(orxFLAG_TEST(sstPhysics.u32Flags, orxPHYSICS_KU32_STATIC_FLAG_ENABLED))
  {
    orxFLOAT fDT;

    /* Stores DT */
    sstPhysics.fLastDT = _pstClockInfo->fDT;

    /* Use fixed time step? */
    if(sstPhysics.fFixedTimeStep)
    {
      orxU32 u32Steps, u32StepsClamped;

      /* Accumulate time */
      sstPhysics.fFixedTimestepAccumulator += _pstClockInfo->fDT;

      /* Compute number of steps */
      u32Steps = orxMath_Floor(sstPhysics.fFixedTimestepAccumulator / sstPhysics.fFixedTimeStep);

      if(u32Steps > 0)
      {
        sstPhysics.fFixedTimestepAccumulator -= u32Steps * sstPhysics.fFixedTimeStep;
      }

      /* Accumulator must have a value lesser than the fixed time step */
      orxASSERT(sstPhysics.fFixedTimestepAccumulator < sstPhysics.fFixedTimeStep + orxMATH_KF_EPSILON);

      /* Update fFixedTimestepAccumulatorRatio */
      sstPhysics.fFixedTimestepAccumulatorRatio = sstPhysics.fFixedTimestepAccumulator / sstPhysics.fFixedTimeStep;

      /* Clamp steps */
      u32StepsClamped = orxMIN(u32Steps, orxPhysics::su32MaxSteps);

      /* For all passed cycles */
      for(orxU32 i = 0; i < u32StepsClamped; i++)
      {
        sstPhysics.poWorld->Step(sstPhysics.fFixedTimeStep, sstPhysics.u32Iterations, sstPhysics.u32Iterations >> 1, sstPhysics.u32ParticleIterations);
      }
    }
    else
    {
      /* For all passed cycles */
      for(fDT = _pstClockInfo->fDT; fDT > orxPhysics::sfMaxDT; fDT -= orxPhysics::sfMaxDT)
      {
        /* Updates world simulation */
        sstPhysics.poWorld->Step(orxPhysics::sfMaxDT, sstPhysics.u32Iterations, sstPhysics.u32Iterations >> 1, sstPhysics.u32ParticleIterations);
      }

      /* Updates last step of world simulation */
      sstPhysics.poWorld->Step(fDT, sstPhysics.u32Iterations, sstPhysics.u32Iterations >> 1, sstPhysics.u32ParticleIterations);
    }

    /* Clears forces */
    sstPhysics.poWorld->ClearForces();

    /* For all physical bodies */
    for(poBody = sstPhysics.poWorld->GetBodyList();
        poBody != NULL;
        poBody = poBody->GetNext())
    {
      /* Non-static and awake? */
      if((poBody->GetType() != b2_staticBody)
      && (poBody->IsAwake() != false))
      {
        /* Applies simulation result */
        orxPhysics_ApplySimulationResult((orxPHYSICS_BODY *)poBody);
      }
    }

    /* For all stored events */
    for(pstEventStorage = (orxPHYSICS_EVENT_STORAGE *)orxLinkList_GetFirst(&(sstPhysics.stEventList));
        pstEventStorage != orxNULL;
        pstEventStorage = (orxPHYSICS_EVENT_STORAGE *)orxLinkList_GetNext(&(pstEventStorage->stNode)))
    {
      /* Depending on type */
      switch(pstEventStorage->eID)
      {
        case orxPHYSICS_EVENT_CONTACT_ADD:
        case orxPHYSICS_EVENT_CONTACT_REMOVE:
        {
          /* New contact? */
          if(pstEventStorage->eID == orxPHYSICS_EVENT_CONTACT_ADD)
          {
            b2Vec2 vPos;

            /* Source can't slide and destination is static? */
            if(!pstEventStorage->poSource->CanSlide() && (pstEventStorage->poDestination->GetType() != b2_dynamicBody))
            {
              /* Gets current position */
              vPos = pstEventStorage->poSource->GetPosition();

              /* Grounds it*/
              vPos.y += 0.01f;

              /* Updates it */
              pstEventStorage->poSource->SetTransform(vPos, pstEventStorage->poSource->GetAngle());
            }
            /* Destination can't slide and source is static? */
            else if(!pstEventStorage->poDestination->CanSlide() && (pstEventStorage->poSource->GetType() != b2_dynamicBody))
            {
              /* Gets current position */
              vPos = pstEventStorage->poDestination->GetPosition();

              /* Grounds it*/
              vPos.y += 0.01f;

              /* Updates it */
              pstEventStorage->poDestination->SetTransform(vPos, pstEventStorage->poDestination->GetAngle());
            }
          }

          /* Sends event */
          orxEVENT_SEND(orxEVENT_TYPE_PHYSICS, pstEventStorage->eID, orxStructure_GetOwner(orxBODY(pstEventStorage->poSource->GetUserData())), orxStructure_GetOwner(orxBODY(pstEventStorage->poDestination->GetUserData())), &(pstEventStorage->stPayload));

          break;
        }

        default:
        {
          break;
        }
      }
    }

    /* Clears stored events */
    orxLinkList_Clean(&(sstPhysics.stEventList));
    orxBank_Clear(sstPhysics.pstEventBank);
  }

  /* Profiles */
  orxPROFILER_POP_MARKER();

  /* Done! */
  return;
}

extern "C" orxPHYSICS_BODY *orxFASTCALL orxPhysics_LiquidFun_CreateBody(const orxHANDLE _hUserData, const orxBODY_DEF *_pstBodyDef)
{
  b2Body     *poResult = 0;
  b2BodyDef   stBodyDef;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_hUserData != orxHANDLE_UNDEFINED);
  orxASSERT(_pstBodyDef != orxNULL);

  /* 2D? */
  if(orxFLAG_TEST(_pstBodyDef->u32Flags, orxBODY_DEF_KU32_FLAG_2D))
  {
    /* Inits body definition */
    stBodyDef.userData          = _hUserData;
    stBodyDef.angle             = _pstBodyDef->fRotation;
    stBodyDef.linearDamping     = _pstBodyDef->fLinearDamping;
    stBodyDef.angularDamping    = _pstBodyDef->fAngularDamping;
    stBodyDef.bullet            = orxFLAG_TEST(_pstBodyDef->u32Flags, orxBODY_DEF_KU32_FLAG_HIGH_SPEED);
    stBodyDef.allowSleep        = orxFLAG_TEST(_pstBodyDef->u32Flags, orxBODY_DEF_KU32_FLAG_ALLOW_SLEEP);
    stBodyDef.fixedRotation     = orxFLAG_TEST(_pstBodyDef->u32Flags, orxBODY_DEF_KU32_FLAG_FIXED_ROTATION);
    stBodyDef.canSlide          = orxFLAG_TEST(_pstBodyDef->u32Flags, orxBODY_DEF_KU32_FLAG_CAN_SLIDE);
    stBodyDef.position.Set(sstPhysics.fDimensionRatio * _pstBodyDef->vPosition.fX, sstPhysics.fDimensionRatio * _pstBodyDef->vPosition.fY);

    /* Is dynamic? */
    if(orxFLAG_TEST(_pstBodyDef->u32Flags, orxBODY_DEF_KU32_FLAG_DYNAMIC))
    {
      b2MassData  stMassData;
      orxBOOL     bHasMass;

      /* Sets its type */
      stBodyDef.type = b2_dynamicBody;

      /* Has mass data? */
      if((_pstBodyDef->fInertia > 0.0f) && (_pstBodyDef->fMass > 0.0f))
      {
        /* Stores mass properties */
        stMassData.I    = _pstBodyDef->fInertia;
        stMassData.mass = _pstBodyDef->fMass;

        /* Updates status */
        bHasMass = orxTRUE;
      }
      else
      {
        /* Updates status */
        bHasMass = orxFALSE;
      }

      /* Creates dynamic body */
      poResult = sstPhysics.poWorld->CreateBody(&stBodyDef);

      /* Valid and has mass data? */
      if((bHasMass != orxFALSE) && (poResult != orxNULL))
      {
        /* Updates its mass data */
        poResult->SetMassData(&stMassData);
      }
    }
    else
    {
      /* Sets its type */
      stBodyDef.type = orxFLAG_TEST(_pstBodyDef->u32Flags, orxBODY_DEF_KU32_FLAG_CAN_MOVE) ? b2_kinematicBody : b2_staticBody;

      /* Creates dynamic body */
      poResult = sstPhysics.poWorld->CreateBody(&stBodyDef);
    }
  }

  /* Done! */
  return (orxPHYSICS_BODY *)poResult;
}

extern "C" void orxFASTCALL orxPhysics_LiquidFun_DeleteBody(orxPHYSICS_BODY *_pstBody)
{
  orxPHYSICS_EVENT_STORAGE *pstEventStorage;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBody != orxNULL);

  /* For all stored events */
  for(pstEventStorage = (orxPHYSICS_EVENT_STORAGE *)orxLinkList_GetFirst(&(sstPhysics.stEventList));
      pstEventStorage != orxNULL;
      pstEventStorage = (pstEventStorage == orxNULL) ? (orxPHYSICS_EVENT_STORAGE *)orxLinkList_GetFirst(&(sstPhysics.stEventList)) : (orxPHYSICS_EVENT_STORAGE *)orxLinkList_GetNext(&(pstEventStorage->stNode)))
  {
    /* Is part of the event? */
    if(((b2Body *)_pstBody == pstEventStorage->poDestination) || ((b2Body *)_pstBody == pstEventStorage->poSource))
    {
      orxPHYSICS_EVENT_STORAGE *pstCurrentEventStorage;

      /* Gets current event */
      pstCurrentEventStorage = pstEventStorage;

      /* Goes back to previous */
      pstEventStorage = (orxPHYSICS_EVENT_STORAGE *)orxLinkList_GetPrevious(&(pstEventStorage->stNode));

      /* Removes event */
      orxLinkList_Remove(&(pstCurrentEventStorage->stNode));
      orxBank_Free(sstPhysics.pstEventBank, pstCurrentEventStorage);
    }
  }

  /* Deletes it */
  sstPhysics.poWorld->DestroyBody((b2Body *)_pstBody);

  return;
}

extern "C" orxPHYSICS_BODY_PART *orxFASTCALL orxPhysics_LiquidFun_CreatePart(orxPHYSICS_BODY *_pstBody, const orxHANDLE _hUserData, const orxBODY_PART_DEF *_pstBodyPartDef)
{
  b2Body         *poBody;
  b2Fixture      *poResult = 0;
  b2FixtureDef    stFixtureDef;
  b2CircleShape   stCircleShape;
  b2PolygonShape  stPolygonShape;
  b2EdgeShape     stEdgeShape;
  b2ChainShape    stChainShape;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBody != orxNULL);
  orxASSERT(_pstBodyPartDef != orxNULL);
  orxASSERT(orxFLAG_TEST(_pstBodyPartDef->u32Flags, orxBODY_PART_DEF_KU32_MASK_TYPE));

  /* Gets body */
  poBody = (b2Body *)_pstBody;

  /* Circle? */
  if(orxFLAG_TEST(_pstBodyPartDef->u32Flags, orxBODY_PART_DEF_KU32_FLAG_SPHERE))
  {
    /* Stores shape reference */
    stFixtureDef.shape = &stCircleShape;

    /* Stores its coordinates */
    stCircleShape.m_p.Set(sstPhysics.fDimensionRatio * _pstBodyPartDef->stSphere.vCenter.fX * _pstBodyPartDef->vScale.fX, sstPhysics.fDimensionRatio * _pstBodyPartDef->stSphere.vCenter.fY * _pstBodyPartDef->vScale.fY);
    stCircleShape.m_radius = sstPhysics.fDimensionRatio * _pstBodyPartDef->stSphere.fRadius * orx2F(0.5f) * (orxMath_Abs(_pstBodyPartDef->vScale.fX) + orxMath_Abs(_pstBodyPartDef->vScale.fY));
  }
  /* Polygon? */
  else if(orxFLAG_TEST(_pstBodyPartDef->u32Flags, orxBODY_PART_DEF_KU32_FLAG_BOX | orxBODY_PART_DEF_KU32_FLAG_MESH))
  {
    /* Stores shape reference */
    stFixtureDef.shape = &stPolygonShape;

    /* Box? */
    if(orxFLAG_TEST(_pstBodyPartDef->u32Flags, orxBODY_PART_DEF_KU32_FLAG_BOX))
    {
      b2Vec2 avVertexList[4];

      /* No mirroring? */
      if(_pstBodyPartDef->vScale.fX * _pstBodyPartDef->vScale.fY > orxFLOAT_0)
      {
        /* Stores its coordinates */
        avVertexList[0].Set(sstPhysics.fDimensionRatio * _pstBodyPartDef->stAABox.stBox.vBR.fX * _pstBodyPartDef->vScale.fX, sstPhysics.fDimensionRatio * _pstBodyPartDef->stAABox.stBox.vTL.fY * _pstBodyPartDef->vScale.fY);
        avVertexList[1].Set(sstPhysics.fDimensionRatio * _pstBodyPartDef->stAABox.stBox.vBR.fX * _pstBodyPartDef->vScale.fX, sstPhysics.fDimensionRatio * _pstBodyPartDef->stAABox.stBox.vBR.fY * _pstBodyPartDef->vScale.fY);
        avVertexList[2].Set(sstPhysics.fDimensionRatio * _pstBodyPartDef->stAABox.stBox.vTL.fX * _pstBodyPartDef->vScale.fX, sstPhysics.fDimensionRatio * _pstBodyPartDef->stAABox.stBox.vBR.fY * _pstBodyPartDef->vScale.fY);
        avVertexList[3].Set(sstPhysics.fDimensionRatio * _pstBodyPartDef->stAABox.stBox.vTL.fX * _pstBodyPartDef->vScale.fX, sstPhysics.fDimensionRatio * _pstBodyPartDef->stAABox.stBox.vTL.fY * _pstBodyPartDef->vScale.fY);
      }
      else
      {
        /* Stores its coordinates */
        avVertexList[0].Set(sstPhysics.fDimensionRatio * _pstBodyPartDef->stAABox.stBox.vTL.fX * _pstBodyPartDef->vScale.fX, sstPhysics.fDimensionRatio * _pstBodyPartDef->stAABox.stBox.vTL.fY * _pstBodyPartDef->vScale.fY);
        avVertexList[1].Set(sstPhysics.fDimensionRatio * _pstBodyPartDef->stAABox.stBox.vTL.fX * _pstBodyPartDef->vScale.fX, sstPhysics.fDimensionRatio * _pstBodyPartDef->stAABox.stBox.vBR.fY * _pstBodyPartDef->vScale.fY);
        avVertexList[2].Set(sstPhysics.fDimensionRatio * _pstBodyPartDef->stAABox.stBox.vBR.fX * _pstBodyPartDef->vScale.fX, sstPhysics.fDimensionRatio * _pstBodyPartDef->stAABox.stBox.vBR.fY * _pstBodyPartDef->vScale.fY);
        avVertexList[3].Set(sstPhysics.fDimensionRatio * _pstBodyPartDef->stAABox.stBox.vBR.fX * _pstBodyPartDef->vScale.fX, sstPhysics.fDimensionRatio * _pstBodyPartDef->stAABox.stBox.vTL.fY * _pstBodyPartDef->vScale.fY);
      }

      /* Updates shape */
      stPolygonShape.Set(avVertexList, 4);
    }
    else
    {
      b2Vec2 avVertexList[b2_maxPolygonVertices];
      orxU32 i;

      /* Checks */
      orxASSERT(_pstBodyPartDef->stMesh.u32VertexCounter > 0);
      orxASSERT(orxBODY_PART_DEF_KU32_MESH_VERTEX_NUMBER <= b2_maxPolygonVertices);

      /* No mirroring? */
      if(_pstBodyPartDef->vScale.fX * _pstBodyPartDef->vScale.fY > orxFLOAT_0)
      {
        /* For all the vertices */
        for(i = 0; i < _pstBodyPartDef->stMesh.u32VertexCounter; i++)
        {
          /* Sets its vector */
          avVertexList[i].Set(sstPhysics.fDimensionRatio * _pstBodyPartDef->stMesh.avVertices[i].fX * _pstBodyPartDef->vScale.fX, sstPhysics.fDimensionRatio * _pstBodyPartDef->stMesh.avVertices[i].fY * _pstBodyPartDef->vScale.fY);
        }
      }
      else
      {
        orxS32 iDst;

        /* For all the vertices */
        for(iDst = _pstBodyPartDef->stMesh.u32VertexCounter - 1, i = 0; iDst >= 0; iDst--, i++)
        {
          /* Sets its vector */
          avVertexList[iDst].Set(sstPhysics.fDimensionRatio * _pstBodyPartDef->stMesh.avVertices[i].fX * _pstBodyPartDef->vScale.fX, sstPhysics.fDimensionRatio * _pstBodyPartDef->stMesh.avVertices[i].fY * _pstBodyPartDef->vScale.fY);
        }
      }

      /* Updates shape */
      stPolygonShape.Set(avVertexList, (int32)_pstBodyPartDef->stMesh.u32VertexCounter);
    }
  }
  else if(orxFLAG_TEST(_pstBodyPartDef->u32Flags, orxBODY_PART_DEF_KU32_FLAG_EDGE))
  {
    b2Vec2 v1, v2;

    /* Stores shape reference */
    stFixtureDef.shape = &stEdgeShape;

    /* No mirroring? */
    if(_pstBodyPartDef->vScale.fX * _pstBodyPartDef->vScale.fY > orxFLOAT_0)
    {
      v1.Set(sstPhysics.fDimensionRatio * _pstBodyPartDef->stEdge.v1.fX * _pstBodyPartDef->vScale.fX, sstPhysics.fDimensionRatio * _pstBodyPartDef->stEdge.v1.fY * _pstBodyPartDef->vScale.fY);
      v2.Set(sstPhysics.fDimensionRatio * _pstBodyPartDef->stEdge.v2.fX * _pstBodyPartDef->vScale.fX, sstPhysics.fDimensionRatio * _pstBodyPartDef->stEdge.v2.fY * _pstBodyPartDef->vScale.fY);

      /* Has vertex0? */
      if(_pstBodyPartDef->stEdge.bHasVertex0)
      {
        b2Vec2 v0;

        v0.Set(sstPhysics.fDimensionRatio * _pstBodyPartDef->stEdge.v0.fX * _pstBodyPartDef->vScale.fX, sstPhysics.fDimensionRatio * _pstBodyPartDef->stEdge.v0.fY * _pstBodyPartDef->vScale.fY);
        stEdgeShape.m_hasVertex0 = true;
        stEdgeShape.m_vertex0 = v0;
      }

      /* Has vertex3? */
      if(_pstBodyPartDef->stEdge.bHasVertex3)
      {
        b2Vec2 v3;

        v3.Set(sstPhysics.fDimensionRatio * _pstBodyPartDef->stEdge.v3.fX * _pstBodyPartDef->vScale.fX, sstPhysics.fDimensionRatio * _pstBodyPartDef->stEdge.v3.fY * _pstBodyPartDef->vScale.fY);
        stEdgeShape.m_hasVertex3 = true;
        stEdgeShape.m_vertex3 = v3;
      }
    }
    else
    {
      v1.Set(sstPhysics.fDimensionRatio * _pstBodyPartDef->stEdge.v2.fX * _pstBodyPartDef->vScale.fX, sstPhysics.fDimensionRatio * _pstBodyPartDef->stEdge.v2.fY * _pstBodyPartDef->vScale.fY);
      v2.Set(sstPhysics.fDimensionRatio * _pstBodyPartDef->stEdge.v1.fX * _pstBodyPartDef->vScale.fX, sstPhysics.fDimensionRatio * _pstBodyPartDef->stEdge.v1.fY * _pstBodyPartDef->vScale.fY);

      /* Has vertex0? */
      if(_pstBodyPartDef->stEdge.bHasVertex0)
      {
        b2Vec2 v0;

        v0.Set(sstPhysics.fDimensionRatio * _pstBodyPartDef->stEdge.v0.fX * _pstBodyPartDef->vScale.fX, sstPhysics.fDimensionRatio * _pstBodyPartDef->stEdge.v0.fY * _pstBodyPartDef->vScale.fY);
        stEdgeShape.m_hasVertex3 = true;
        stEdgeShape.m_vertex3 = v0;
      }

      /* Has vertex3? */
      if(_pstBodyPartDef->stEdge.bHasVertex3)
      {
        b2Vec2 v3;

        v3.Set(sstPhysics.fDimensionRatio * _pstBodyPartDef->stEdge.v3.fX * _pstBodyPartDef->vScale.fX, sstPhysics.fDimensionRatio * _pstBodyPartDef->stEdge.v3.fY * _pstBodyPartDef->vScale.fY);
        stEdgeShape.m_hasVertex0 = true;
        stEdgeShape.m_vertex0 = v3;
      }
    }

    /* Updates shape */
    stEdgeShape.Set(v1, v2);
  }
  else if(orxFLAG_TEST(_pstBodyPartDef->u32Flags, orxBODY_PART_DEF_KU32_FLAG_CHAIN))
  {
    /* Checks */
    orxASSERT(_pstBodyPartDef->stChain.u32VertexCounter > 0);
    orxASSERT(_pstBodyPartDef->stChain.avVertices != orxNULL);

    /* Stores shape reference */
    stFixtureDef.shape = &stChainShape;

    b2Vec2 *avVertexList = (b2Vec2*) alloca(_pstBodyPartDef->stChain.u32VertexCounter * sizeof(b2Vec2));
    orxU32 i;

    /* No mirroring? */
    if(_pstBodyPartDef->vScale.fX * _pstBodyPartDef->vScale.fY > orxFLOAT_0)
    {
      /* For all the vertices */
      for(i = 0; i < _pstBodyPartDef->stChain.u32VertexCounter; i++)
      {
        /* Sets its vector */
        avVertexList[i].Set(sstPhysics.fDimensionRatio * _pstBodyPartDef->stChain.avVertices[i].fX * _pstBodyPartDef->vScale.fX, sstPhysics.fDimensionRatio * _pstBodyPartDef->stChain.avVertices[i].fY * _pstBodyPartDef->vScale.fY);
      }

      stChainShape.CreateChain(avVertexList, _pstBodyPartDef->stChain.u32VertexCounter);

      /* Has Prev? */
      if(_pstBodyPartDef->stChain.bHasPrev)
      {
        b2Vec2 vPrev;

        vPrev.Set(sstPhysics.fDimensionRatio * _pstBodyPartDef->stChain.vPrev.fX * _pstBodyPartDef->vScale.fX, sstPhysics.fDimensionRatio * _pstBodyPartDef->stChain.vPrev.fY * _pstBodyPartDef->vScale.fY);
        stChainShape.SetPrevVertex(vPrev);
      }

      /* Has Next? */
      if(_pstBodyPartDef->stChain.bHasNext)
      {
        b2Vec2 vNext;

        vNext.Set(sstPhysics.fDimensionRatio * _pstBodyPartDef->stChain.vNext.fX * _pstBodyPartDef->vScale.fX, sstPhysics.fDimensionRatio * _pstBodyPartDef->stChain.vNext.fY * _pstBodyPartDef->vScale.fY);
        stChainShape.SetNextVertex(vNext);
      }
    }
    else
    {
      orxS32 iDst;

      /* For all the vertices */
      for(iDst = _pstBodyPartDef->stChain.u32VertexCounter - 1, i = 0; iDst >= 0; iDst--, i++)
      {
        /* Sets its vector */
        avVertexList[iDst].Set(sstPhysics.fDimensionRatio * _pstBodyPartDef->stChain.avVertices[i].fX * _pstBodyPartDef->vScale.fX, sstPhysics.fDimensionRatio * _pstBodyPartDef->stChain.avVertices[i].fY * _pstBodyPartDef->vScale.fY);
      }

      stChainShape.CreateChain(avVertexList, _pstBodyPartDef->stChain.u32VertexCounter);

      /* Has Prev? */
      if(_pstBodyPartDef->stChain.bHasPrev)
      {
        b2Vec2 vPrev;

        vPrev.Set(sstPhysics.fDimensionRatio * _pstBodyPartDef->stChain.vPrev.fX * _pstBodyPartDef->vScale.fX, sstPhysics.fDimensionRatio * _pstBodyPartDef->stChain.vPrev.fY * _pstBodyPartDef->vScale.fY);
        stChainShape.SetNextVertex(vPrev);
      }

      /* Has Next? */
      if(_pstBodyPartDef->stChain.bHasNext)
      {
        b2Vec2 vNext;

        vNext.Set(sstPhysics.fDimensionRatio * _pstBodyPartDef->stChain.vNext.fX * _pstBodyPartDef->vScale.fX, sstPhysics.fDimensionRatio * _pstBodyPartDef->stChain.vNext.fY * _pstBodyPartDef->vScale.fY);
        stChainShape.SetPrevVertex(vNext);
      }
    }
  }

  /* Inits Fixture definition */
  stFixtureDef.userData             = _hUserData;
  stFixtureDef.friction             = _pstBodyPartDef->fFriction;
  stFixtureDef.restitution          = _pstBodyPartDef->fRestitution;
  stFixtureDef.density              = (poBody->GetType() != b2_dynamicBody) ? 0.0f : _pstBodyPartDef->fDensity;
  stFixtureDef.filter.categoryBits  = _pstBodyPartDef->u16SelfFlags;
  stFixtureDef.filter.maskBits      = _pstBodyPartDef->u16CheckMask;
  stFixtureDef.filter.groupIndex    = 0;
  stFixtureDef.isSensor             = orxFLAG_TEST(_pstBodyPartDef->u32Flags, orxBODY_PART_DEF_KU32_FLAG_SOLID) == orxFALSE;

  /* Creates it */
  poResult = poBody->CreateFixture(&stFixtureDef);

  /* Done! */
  return (orxPHYSICS_BODY_PART *)poResult;
}

extern "C" void orxFASTCALL orxPhysics_LiquidFun_DeletePart(orxPHYSICS_BODY_PART *_pstBodyPart)
{
  b2Fixture  *poFixture;
  b2Body     *poBody;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBodyPart != orxNULL);

  /* Gets Fixture */
  poFixture = (b2Fixture *)_pstBodyPart;

  /* Gets its body */
  poBody = poFixture->GetBody();

  /* Deletes its part */
  poBody->DestroyFixture(poFixture);

  return;
}

extern "C" orxPHYSICS_BODY_JOINT *orxFASTCALL orxPhysics_LiquidFun_CreateJoint(orxPHYSICS_BODY *_pstSrcBody, orxPHYSICS_BODY *_pstDstBody, const orxHANDLE _hUserData, const orxBODY_JOINT_DEF *_pstBodyJointDef)
{
  b2Joint            *poResult = 0;
  orxBOOL             bSuccess = orxTRUE;
  b2JointDef         *pstJointDef;
  b2RevoluteJointDef  stRevoluteJointDef;
  b2PrismaticJointDef stPrismaticJointDef;
  b2DistanceJointDef  stSpringJointDef;
  b2RopeJointDef      stRopeJointDef;
  b2PulleyJointDef    stPulleyJointDef;
  b2WheelJointDef     stSuspensionJointDef;
  b2WeldJointDef      stWeldJointDef;
  b2FrictionJointDef  stFrictionJointDef;
  b2GearJointDef      stGearJointDef;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstSrcBody != orxNULL);
  orxASSERT(_pstDstBody != orxNULL);
  orxASSERT(_hUserData != orxHANDLE_UNDEFINED);
  orxASSERT(_pstBodyJointDef != orxNULL);
  orxASSERT(orxFLAG_TEST(_pstBodyJointDef->u32Flags, orxBODY_JOINT_DEF_KU32_MASK_TYPE));

  /* Depending on joint type */
  switch(orxFLAG_GET(_pstBodyJointDef->u32Flags, orxBODY_JOINT_DEF_KU32_MASK_TYPE))
  {
    /* Revolute? */
    case orxBODY_JOINT_DEF_KU32_FLAG_REVOLUTE:
    {
      /* Stores joint reference */
      pstJointDef = &stRevoluteJointDef;

      /* Stores anchors */
      stRevoluteJointDef.localAnchorA.Set(sstPhysics.fDimensionRatio * _pstBodyJointDef->vSrcScale.fX * _pstBodyJointDef->vSrcAnchor.fX, sstPhysics.fDimensionRatio * _pstBodyJointDef->vSrcScale.fY * _pstBodyJointDef->vSrcAnchor.fY);
      stRevoluteJointDef.localAnchorB.Set(sstPhysics.fDimensionRatio * _pstBodyJointDef->vDstScale.fX * _pstBodyJointDef->vDstAnchor.fX, sstPhysics.fDimensionRatio * _pstBodyJointDef->vDstScale.fY * _pstBodyJointDef->vDstAnchor.fY);

      /* Stores reference angle */
      stRevoluteJointDef.referenceAngle = _pstBodyJointDef->stRevolute.fDefaultRotation;

      /* Has rotation limits? */
      if(orxFLAG_TEST(_pstBodyJointDef->u32Flags, orxBODY_JOINT_DEF_KU32_FLAG_ROTATION_LIMIT))
      {
        /* Stores them */
        stRevoluteJointDef.lowerAngle   = _pstBodyJointDef->stRevolute.fMinRotation;
        stRevoluteJointDef.upperAngle   = _pstBodyJointDef->stRevolute.fMaxRotation;

        /* Updates status */
        stRevoluteJointDef.enableLimit  = true;
      }

      /* Is motor? */
      if(orxFLAG_TEST(_pstBodyJointDef->u32Flags, orxBODY_JOINT_DEF_KU32_FLAG_MOTOR))
      {
        /* Stores them */
        stRevoluteJointDef.motorSpeed     = _pstBodyJointDef->stRevolute.fMotorSpeed;
        stRevoluteJointDef.maxMotorTorque = _pstBodyJointDef->stRevolute.fMaxMotorTorque;

        /* Updates status */
        stRevoluteJointDef.enableMotor    = true;
      }

      break;
    }

    /* Prismatic? */
    case orxBODY_JOINT_DEF_KU32_FLAG_PRISMATIC:
    {
      /* Stores joint reference */
      pstJointDef = &stPrismaticJointDef;

      /* Stores anchors */
      stPrismaticJointDef.localAnchorA.Set(sstPhysics.fDimensionRatio * _pstBodyJointDef->vSrcScale.fX * _pstBodyJointDef->vSrcAnchor.fX, sstPhysics.fDimensionRatio * _pstBodyJointDef->vSrcScale.fY * _pstBodyJointDef->vSrcAnchor.fY);
      stPrismaticJointDef.localAnchorB.Set(sstPhysics.fDimensionRatio * _pstBodyJointDef->vDstScale.fX * _pstBodyJointDef->vDstAnchor.fX, sstPhysics.fDimensionRatio * _pstBodyJointDef->vDstScale.fY * _pstBodyJointDef->vDstAnchor.fY);

      /* Stores reference angle */
      stPrismaticJointDef.referenceAngle = _pstBodyJointDef->stPrismatic.fDefaultRotation;

      /* Stores translation axis */
      stPrismaticJointDef.localAxisA.Set(_pstBodyJointDef->stPrismatic.vTranslationAxis.fX, _pstBodyJointDef->stPrismatic.vTranslationAxis.fY);

      /* Has translation limits? */
      if(orxFLAG_TEST(_pstBodyJointDef->u32Flags, orxBODY_JOINT_DEF_KU32_FLAG_TRANSLATION_LIMIT))
      {
        /* Stores them */
        stPrismaticJointDef.lowerTranslation  = sstPhysics.fDimensionRatio * _pstBodyJointDef->stPrismatic.fMinTranslation;
        stPrismaticJointDef.upperTranslation  = sstPhysics.fDimensionRatio * _pstBodyJointDef->stPrismatic.fMaxTranslation;

        /* Updates status */
        stPrismaticJointDef.enableLimit       = true;
      }

      /* Is motor? */
      if(orxFLAG_TEST(_pstBodyJointDef->u32Flags, orxBODY_JOINT_DEF_KU32_FLAG_MOTOR))
      {
        /* Stores them */
        stPrismaticJointDef.motorSpeed    = sstPhysics.fDimensionRatio * _pstBodyJointDef->stPrismatic.fMotorSpeed;
        stPrismaticJointDef.maxMotorForce = _pstBodyJointDef->stPrismatic.fMaxMotorForce;

        /* Updates status */
        stPrismaticJointDef.enableMotor   = true;
      }

      break;
    }

    /* Spring? */
    case orxBODY_JOINT_DEF_KU32_FLAG_SPRING:
    {
      /* Stores joint reference */
      pstJointDef = &stSpringJointDef;

      /* Stores anchors */
      stSpringJointDef.localAnchorA.Set(sstPhysics.fDimensionRatio * _pstBodyJointDef->vSrcScale.fX * _pstBodyJointDef->vSrcAnchor.fX, sstPhysics.fDimensionRatio * _pstBodyJointDef->vSrcScale.fY * _pstBodyJointDef->vSrcAnchor.fY);
      stSpringJointDef.localAnchorB.Set(sstPhysics.fDimensionRatio * _pstBodyJointDef->vDstScale.fX * _pstBodyJointDef->vDstAnchor.fX, sstPhysics.fDimensionRatio * _pstBodyJointDef->vDstScale.fY * _pstBodyJointDef->vDstAnchor.fY);

      /* Stores length */
      stSpringJointDef.length       = sstPhysics.fDimensionRatio * _pstBodyJointDef->stSpring.fLength;

      /* Stores frequency */
      stSpringJointDef.frequencyHz  = _pstBodyJointDef->stSpring.fFrequency;

      /* Stores damping ratio */
      stSpringJointDef.dampingRatio = _pstBodyJointDef->stSpring.fDamping;

      break;
    }
    /* Rope? */
    case orxBODY_JOINT_DEF_KU32_FLAG_ROPE:
    {
      /* Stores joint reference */
      pstJointDef = &stRopeJointDef;

      /* Stores anchors */
      stRopeJointDef.localAnchorA.Set(sstPhysics.fDimensionRatio * _pstBodyJointDef->vSrcScale.fX * _pstBodyJointDef->vSrcAnchor.fX, sstPhysics.fDimensionRatio * _pstBodyJointDef->vSrcScale.fY * _pstBodyJointDef->vSrcAnchor.fY);
      stRopeJointDef.localAnchorB.Set(sstPhysics.fDimensionRatio * _pstBodyJointDef->vDstScale.fX * _pstBodyJointDef->vDstAnchor.fX, sstPhysics.fDimensionRatio * _pstBodyJointDef->vDstScale.fY * _pstBodyJointDef->vDstAnchor.fY);

      /* Stores length */
      stRopeJointDef.maxLength = sstPhysics.fDimensionRatio * _pstBodyJointDef->stRope.fLength;

      break;
    }
    /* Pulley? */
    case orxBODY_JOINT_DEF_KU32_FLAG_PULLEY:
    {
      /* Stores joint reference */
      pstJointDef = &stPulleyJointDef;

      /* Stores anchors */
      stPulleyJointDef.localAnchorA.Set(sstPhysics.fDimensionRatio * _pstBodyJointDef->vSrcScale.fX * _pstBodyJointDef->vSrcAnchor.fX, sstPhysics.fDimensionRatio * _pstBodyJointDef->vSrcScale.fY * _pstBodyJointDef->vSrcAnchor.fY);
      stPulleyJointDef.localAnchorB.Set(sstPhysics.fDimensionRatio * _pstBodyJointDef->vDstScale.fX * _pstBodyJointDef->vDstAnchor.fX, sstPhysics.fDimensionRatio * _pstBodyJointDef->vDstScale.fY * _pstBodyJointDef->vDstAnchor.fY);
      stPulleyJointDef.groundAnchorA.Set(sstPhysics.fDimensionRatio * _pstBodyJointDef->stPulley.vSrcGroundAnchor.fX, sstPhysics.fDimensionRatio * _pstBodyJointDef->stPulley.vSrcGroundAnchor.fY);
      stPulleyJointDef.groundAnchorB.Set(sstPhysics.fDimensionRatio * _pstBodyJointDef->stPulley.vDstGroundAnchor.fX, sstPhysics.fDimensionRatio * _pstBodyJointDef->stPulley.vDstGroundAnchor.fY);

      /* Stores lengths */
      stPulleyJointDef.lengthA    = sstPhysics.fDimensionRatio * _pstBodyJointDef->stPulley.fSrcLength;
// missing     stPulleyJointDef.maxLengthA = sstPhysics.fDimensionRatio * _pstBodyJointDef->stPulley.fMaxSrcLength;
      stPulleyJointDef.lengthB    = sstPhysics.fDimensionRatio * _pstBodyJointDef->stPulley.fDstLength;
// missing     stPulleyJointDef.maxLengthB = sstPhysics.fDimensionRatio * _pstBodyJointDef->stPulley.fMaxDstLength;

      /* Stores ratio */
      stPulleyJointDef.ratio      = _pstBodyJointDef->stPulley.fLengthRatio;

      break;
    }

    /* Suspension? */
    case orxBODY_JOINT_DEF_KU32_FLAG_SUSPENSION:
    {
      /* Stores joint reference */
      pstJointDef = &stSuspensionJointDef;

      /* Stores anchors */
      stSuspensionJointDef.localAnchorA.Set(sstPhysics.fDimensionRatio * _pstBodyJointDef->vSrcScale.fX * _pstBodyJointDef->vSrcAnchor.fX, sstPhysics.fDimensionRatio * _pstBodyJointDef->vSrcScale.fY * _pstBodyJointDef->vSrcAnchor.fY);
      stSuspensionJointDef.localAnchorB.Set(sstPhysics.fDimensionRatio * _pstBodyJointDef->vDstScale.fX * _pstBodyJointDef->vDstAnchor.fX, sstPhysics.fDimensionRatio * _pstBodyJointDef->vDstScale.fY * _pstBodyJointDef->vDstAnchor.fY);

      /* Stores translation axis */
      stSuspensionJointDef.localAxisA.Set(_pstBodyJointDef->stSuspension.vTranslationAxis.fX, _pstBodyJointDef->stSuspension.vTranslationAxis.fY);

      /* Has translation limits? */
      if(orxFLAG_TEST(_pstBodyJointDef->u32Flags, orxBODY_JOINT_DEF_KU32_FLAG_TRANSLATION_LIMIT))
      {
        /* Stores them */
// missing       stSuspensionJointDef.lowerTranslation  = sstPhysics.fDimensionRatio * _pstBodyJointDef->stSuspension.fMinTranslation;
// missing       stSuspensionJointDef.upperTranslation  = sstPhysics.fDimensionRatio * _pstBodyJointDef->stSuspension.fMaxTranslation;

        /* Updates status */
// missing       stSuspensionJointDef.enableLimit  = true;
      }

      /* Is motor? */
      if(orxFLAG_TEST(_pstBodyJointDef->u32Flags, orxBODY_JOINT_DEF_KU32_FLAG_MOTOR))
      {
        /* Stores them */
        stSuspensionJointDef.motorSpeed    = sstPhysics.fDimensionRatio * _pstBodyJointDef->stSuspension.fMotorSpeed;
        stSuspensionJointDef.maxMotorTorque = _pstBodyJointDef->stSuspension.fMaxMotorForce;

        /* Updates status */
        stSuspensionJointDef.enableMotor    = true;
      }

      break;
    }

    /* Weld? */
    case orxBODY_JOINT_DEF_KU32_FLAG_WELD:
    {
      /* Stores joint reference */
      pstJointDef = &stWeldJointDef;

      /* Stores anchors */
      stWeldJointDef.localAnchorA.Set(sstPhysics.fDimensionRatio * _pstBodyJointDef->vSrcScale.fX * _pstBodyJointDef->vSrcAnchor.fX, sstPhysics.fDimensionRatio * _pstBodyJointDef->vSrcScale.fY * _pstBodyJointDef->vSrcAnchor.fY);
      stWeldJointDef.localAnchorB.Set(sstPhysics.fDimensionRatio * _pstBodyJointDef->vDstScale.fX * _pstBodyJointDef->vDstAnchor.fX, sstPhysics.fDimensionRatio * _pstBodyJointDef->vDstScale.fY * _pstBodyJointDef->vDstAnchor.fY);

      /* Stores reference angle */
      stWeldJointDef.referenceAngle = _pstBodyJointDef->stWeld.fDefaultRotation;

      break;
    }

    /* Friction? */
    case orxBODY_JOINT_DEF_KU32_FLAG_FRICTION:
    {
      /* Stores joint reference */
      pstJointDef = &stFrictionJointDef;

      /* Stores anchors */
      stFrictionJointDef.localAnchorA.Set(sstPhysics.fDimensionRatio * _pstBodyJointDef->vSrcScale.fX * _pstBodyJointDef->vSrcAnchor.fX, sstPhysics.fDimensionRatio * _pstBodyJointDef->vSrcScale.fY * _pstBodyJointDef->vSrcAnchor.fY);
      stFrictionJointDef.localAnchorB.Set(sstPhysics.fDimensionRatio * _pstBodyJointDef->vDstScale.fX * _pstBodyJointDef->vDstAnchor.fX, sstPhysics.fDimensionRatio * _pstBodyJointDef->vDstScale.fY * _pstBodyJointDef->vDstAnchor.fY);

      /* Stores max force & torque values */
      stFrictionJointDef.maxForce   = _pstBodyJointDef->stFriction.fMaxForce;
      stFrictionJointDef.maxTorque  = _pstBodyJointDef->stFriction.fMaxTorque;

      break;
    }

    /* Gear? */
    case orxBODY_JOINT_DEF_KU32_FLAG_GEAR:
    {
      b2Body *poBody;

      /* Stores joint reference */
      pstJointDef = &stGearJointDef;

      /* Stores ratio */
      stGearJointDef.ratio = _pstBodyJointDef->stGear.fJointRatio;

      /* Gets source body */
      poBody = (b2Body *)_pstSrcBody;

      /* For all its joints */
      for(b2JointEdge *poEdge = poBody->GetJointList();
          poEdge != 0;
          poEdge = poEdge->next)
      {
        b2Joint        *poJoint;
        orxBODY_JOINT  *pstJoint;

        /* Gets it */
        poJoint = poEdge->joint;

        /* Gets its body joint */
        pstJoint = (orxBODY_JOINT *)poJoint->GetUserData();

        /* Does name match? */
        if(orxString_Compare(orxBody_GetJointName(pstJoint), _pstBodyJointDef->stGear.zSrcJointName) == 0)
        {
          /* Stores it */
          stGearJointDef.joint1 = poJoint;
          break;
        }
      }

      /* Found source joint? */
      if(stGearJointDef.joint1 != 0)
      {
        /* Gets destination body */
        poBody = (b2Body *)_pstDstBody;

        /* For all its joints */
        for(b2JointEdge *poEdge = poBody->GetJointList();
            poEdge != 0;
            poEdge = poEdge->next)
        {
          b2Joint        *poJoint;
          orxBODY_JOINT  *pstJoint;

          /* Gets it */
          poJoint = poEdge->joint;

          /* Gets its body joint */
          pstJoint = (orxBODY_JOINT *)poJoint->GetUserData();

          /* Does name match? */
          if(orxString_Compare(orxBody_GetJointName(pstJoint), _pstBodyJointDef->stGear.zDstJointName) == 0)
          {
            /* Stores it */
            stGearJointDef.joint2 = poJoint;
            break;
          }
        }

        /* No destination joint found? */
        if(stGearJointDef.joint2 == 0)
        {
          /* Logs message */
          orxDEBUG_PRINT(orxDEBUG_LEVEL_PHYSICS, "Can't create gear body joint, couldn't find joint <%s> on destination body.", _pstBodyJointDef->stGear.zDstJointName);

          /* Updates status */
          bSuccess = orxFALSE;
        }
      }
      else
      {
        /* Logs message */
        orxDEBUG_PRINT(orxDEBUG_LEVEL_PHYSICS, "Can't create gear body joint, couldn't find joint <%s> on source body.", _pstBodyJointDef->stGear.zSrcJointName);

        /* Updates status */
        bSuccess = orxFALSE;
      }

      break;
    }

    default:
    {
      /* Logs message */
      orxDEBUG_PRINT(orxDEBUG_LEVEL_PHYSICS, "Can't create body joint, invalid type <0x%X>.", orxFLAG_GET(_pstBodyJointDef->u32Flags, orxBODY_JOINT_DEF_KU32_MASK_TYPE));

      /* Updates status */
      bSuccess = orxFALSE;

      break;
    }
  }

  /* Valid? */
  if(bSuccess != orxFALSE)
  {
    /* Inits joint definition */
    pstJointDef->userData             = _hUserData;
    pstJointDef->bodyA                = (b2Body *)_pstSrcBody;
    pstJointDef->bodyB                = (b2Body *)_pstDstBody;
    pstJointDef->collideConnected     = orxFLAG_TEST(_pstBodyJointDef->u32Flags, orxBODY_JOINT_DEF_KU32_FLAG_COLLIDE) ? true : false;

    /* Creates it */
    poResult = sstPhysics.poWorld->CreateJoint(pstJointDef);
  }

  /* Done! */
  return (orxPHYSICS_BODY_JOINT *)poResult;
}

extern "C" void orxFASTCALL orxPhysics_LiquidFun_DeleteJoint(orxPHYSICS_BODY_JOINT *_pstBodyJoint)
{
  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBodyJoint != orxNULL);

  /* Deletes it */
  sstPhysics.poWorld->DestroyJoint((b2Joint *)_pstBodyJoint);

  return;
}

extern "C" void orxFASTCALL orxPhysics_LiquidFun_EnableMotor(orxPHYSICS_BODY_JOINT *_pstBodyJoint, orxBOOL _bEnable)
{
  b2Joint *poJoint;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBodyJoint != orxNULL);

  /* Gets joint */
  poJoint = (b2Joint *)_pstBodyJoint;

  /* Is a revolute joint? */
  if(poJoint->GetType() == e_revoluteJoint)
  {
    /* Enables / disables it */
    static_cast<b2RevoluteJoint *>(poJoint)->EnableMotor((_bEnable != orxFALSE) ? true : false);
  }
  else
  {
    /* Logs message */
    orxDEBUG_PRINT(orxDEBUG_LEVEL_PHYSICS, "Can't enable motor on non-revolute joint.");
  }

  /* Done! */
  return;
}

extern "C" void orxFASTCALL orxPhysics_LiquidFun_SetJointMotorSpeed(orxPHYSICS_BODY_JOINT *_pstBodyJoint, orxFLOAT _fSpeed)
{
  b2Joint *poJoint;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBodyJoint != orxNULL);

  /* Gets joint */
  poJoint = (b2Joint *)_pstBodyJoint;

  /* Is a revolute joint? */
  if(poJoint->GetType() == e_revoluteJoint)
  {
    /* Sets its motor speed */
    static_cast<b2RevoluteJoint *>(poJoint)->SetMotorSpeed(_fSpeed);
  }
  else
  {
    /* Logs message */
    orxDEBUG_PRINT(orxDEBUG_LEVEL_PHYSICS, "Can't set motor speed on non-revolute joint.");
  }

  /* Done! */
  return;
}

extern "C" void orxFASTCALL orxPhysics_LiquidFun_SetJointMaxMotorTorque(orxPHYSICS_BODY_JOINT *_pstBodyJoint, orxFLOAT _fMaxTorque)
{
  b2Joint *poJoint;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBodyJoint != orxNULL);

  /* Gets joint */
  poJoint = (b2Joint *)_pstBodyJoint;

  /* Is a revolute joint? */
  if(poJoint->GetType() == e_revoluteJoint)
  {
    /* Sets its max torque */
    static_cast<b2RevoluteJoint *>(poJoint)->SetMaxMotorTorque(_fMaxTorque);
  }
  else
  {
    /* Logs message */
    orxDEBUG_PRINT(orxDEBUG_LEVEL_PHYSICS, "Can't set max motor torque on non-revolute joint.");
  }

  /* Done! */
  return;
}

extern "C" orxVECTOR *orxFASTCALL orxPhysics_LiquidFun_GetJointReactionForce(const orxPHYSICS_BODY_JOINT *_pstBodyJoint, orxVECTOR *_pvForce)
{
  const b2Joint  *poJoint;
  b2Vec2          vForce;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBodyJoint != orxNULL);
  orxASSERT(_pvForce != orxNULL);

  /* Gets joint */
  poJoint = (const b2Joint *)_pstBodyJoint;

  /* Gets reaction force */
  vForce = poJoint->GetReactionForce((sstPhysics.fLastDT != orxFLOAT_0) ? orxFLOAT_1 / sstPhysics.fLastDT : orxFLOAT_1 / orxPhysics::sfMaxDT);

  /* Updates result */
  orxVector_Set(_pvForce, orx2F(vForce.x), orx2F(vForce.y), orxFLOAT_0);

  /* Done! */
  return _pvForce;
}

extern "C" orxFLOAT orxFASTCALL orxPhysics_LiquidFun_GetJointReactionTorque(const orxPHYSICS_BODY_JOINT *_pstBodyJoint)
{
  const b2Joint  *poJoint;
  orxFLOAT        fResult;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBodyJoint != orxNULL);

  /* Gets joint */
  poJoint = (const b2Joint *)_pstBodyJoint;

  /* Updates result */
  fResult = orx2F(poJoint->GetReactionTorque((sstPhysics.fLastDT != orxFLOAT_0) ? orxFLOAT_1 / sstPhysics.fLastDT : orxFLOAT_1 / orxPhysics::sfMaxDT));

  /* Done! */
  return fResult;
}

extern "C" orxSTATUS orxFASTCALL orxPhysics_LiquidFun_SetPosition(orxPHYSICS_BODY *_pstBody, const orxVECTOR *_pvPosition)
{
  b2Body   *poBody;
  b2Vec2    vPosition;
  orxSTATUS eResult = orxSTATUS_SUCCESS;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBody != orxNULL);
  orxASSERT(_pvPosition != orxNULL);

  /* Gets body */
  poBody = (b2Body *)_pstBody;

  /* Gets its position */
  const b2Vec2 &rvPos = poBody->GetPosition();

  /* Sets position vector */
  vPosition.Set(sstPhysics.fDimensionRatio * _pvPosition->fX, sstPhysics.fDimensionRatio * _pvPosition->fY);

  /* Should apply? */
  if((rvPos.x != vPosition.x) || (rvPos.y != vPosition.y))
  {
    /* Wakes up */
    poBody->SetAwake(true);

    /* Updates its position */
    poBody->SetTransform(vPosition, poBody->GetAngle());
  }

  /* Done! */
  return eResult;
}

extern "C" orxSTATUS orxFASTCALL orxPhysics_LiquidFun_SetRotation(orxPHYSICS_BODY *_pstBody, orxFLOAT _fRotation)
{
  b2Body   *poBody;
  float32   fRotation;
  orxSTATUS eResult = orxSTATUS_SUCCESS;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBody != orxNULL);

  /* Gets body */
  poBody = (b2Body *)_pstBody;

  /* Gets its rotation */
  fRotation = poBody->GetAngle();

  /* Should apply? */
  if(fRotation != _fRotation)
  {
    /* Wakes up */
    poBody->SetAwake(true);

    /* Updates its rotation */
    poBody->SetTransform(poBody->GetPosition(), _fRotation);
  }

  /* Done! */
  return eResult;
}

extern "C" orxSTATUS orxFASTCALL orxPhysics_LiquidFun_SetSpeed(orxPHYSICS_BODY *_pstBody, const orxVECTOR *_pvSpeed)
{
  b2Body   *poBody;
  b2Vec2    vSpeed;
  orxSTATUS eResult = orxSTATUS_SUCCESS;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBody != orxNULL);
  orxASSERT(_pvSpeed != orxNULL);

  /* Gets body */
  poBody = (b2Body *)_pstBody;

  /* Gets its speed */
  const b2Vec2 &rvSpeed = poBody->GetLinearVelocity();

  /* Sets speed vector */
  vSpeed.Set(sstPhysics.fDimensionRatio * _pvSpeed->fX, sstPhysics.fDimensionRatio * _pvSpeed->fY);

  /* Should apply? */
  if((rvSpeed.x != vSpeed.x) || (rvSpeed.y != vSpeed.y))
  {
    /* Wakes up */
    poBody->SetAwake(true);

    /* Updates its speed */
    poBody->SetLinearVelocity(vSpeed);
  }

  /* Done! */
  return eResult;
}

extern "C" orxSTATUS orxFASTCALL orxPhysics_LiquidFun_SetAngularVelocity(orxPHYSICS_BODY *_pstBody, orxFLOAT _fVelocity)
{
  b2Body   *poBody;
  float32   fAngularVelocity;
  orxSTATUS eResult = orxSTATUS_SUCCESS;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBody != orxNULL);

  /* Gets body */
  poBody = (b2Body *)_pstBody;

  /* Gets its angular velocity */
  fAngularVelocity = poBody->GetAngularVelocity();

  /* Should apply? */
  if(fAngularVelocity != _fVelocity)
  {
    /* Wakes up */
    poBody->SetAwake(true);

    /* Updates its angular velocity */
    poBody->SetAngularVelocity(_fVelocity);
  }

  /* Done! */
  return eResult;
}

extern "C" orxSTATUS orxFASTCALL orxPhysics_LiquidFun_SetCustomGravity(orxPHYSICS_BODY *_pstBody, const orxVECTOR *_pvCustomGravity)
{
  b2Body       *poBody;
  const b2Vec2 *pvCustomGravity;
  orxSTATUS     eResult = orxSTATUS_SUCCESS;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBody != orxNULL);

  /* Gets body */
  poBody = (b2Body *)_pstBody;

  /* Gets its custom gravity */
  pvCustomGravity = poBody->GetCustomGravity();

  /* Has new custom gravity? */
  if(_pvCustomGravity != orxNULL)
  {
    b2Vec2 vGravity;

    /* Sets gravity vector */
    vGravity.Set(sstPhysics.fDimensionRatio * _pvCustomGravity->fX, sstPhysics.fDimensionRatio * _pvCustomGravity->fY);

    /* Should apply? */
    if((pvCustomGravity == NULL) || (pvCustomGravity->x != vGravity.x) || (pvCustomGravity->y != vGravity.y))
    {
      /* Wakes up */
      poBody->SetAwake(true);

      /* Updates it */
      poBody->SetCustomGravity(&vGravity);
    }
  }
  else
  {
    /* Should apply */
    if(pvCustomGravity != NULL)
    {
      /* Wakes up */
      poBody->SetAwake(true);

      /* Removes it */
      poBody->SetCustomGravity(orxNULL);
    }
  }

  /* Done! */
  return eResult;
}

extern "C" orxSTATUS orxFASTCALL orxPhysics_LiquidFun_SetFixedRotation(orxPHYSICS_BODY * _pstBody, orxBOOL _bFixed)
{
  b2Body   *poBody;
  orxSTATUS eResult = orxSTATUS_SUCCESS;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBody != orxNULL);

  /* Gets body */
  poBody = (b2Body *)_pstBody;

  /* Updates its fixed rotation property */
  poBody->SetFixedRotation((_bFixed != orxFALSE) ? true : false);

  /* Done! */
  return eResult;
}

extern "C" orxVECTOR *orxFASTCALL orxPhysics_LiquidFun_GetPosition(const orxPHYSICS_BODY *_pstBody, orxVECTOR *_pvPosition)
{
  b2Body     *poBody;
  b2Vec2      vPosition;
  orxVECTOR  *pvResult;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBody != orxNULL);
  orxASSERT(_pvPosition != orxNULL);

  /* Gets body */
  poBody = (b2Body *)_pstBody;

  /* Gets its position */
  vPosition = poBody->GetPosition();

  /* Updates result */
  pvResult      = _pvPosition;
  pvResult->fX  = sstPhysics.fRecDimensionRatio * vPosition.x;
  pvResult->fY  = sstPhysics.fRecDimensionRatio * vPosition.y;

  /* Done! */
  return pvResult;
}

extern "C" orxFLOAT orxFASTCALL orxPhysics_LiquidFun_GetRotation(const orxPHYSICS_BODY *_pstBody)
{
  b2Body   *poBody;
  orxFLOAT  fResult;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBody != orxNULL);

  /* Gets body */
  poBody = (b2Body *)_pstBody;

  /* Gets its rotation */
  fResult = poBody->GetAngle();

  /* Done! */
  return fResult;
}

extern "C" orxVECTOR *orxFASTCALL orxPhysics_LiquidFun_GetSpeed(const orxPHYSICS_BODY *_pstBody, orxVECTOR *_pvSpeed)
{
  b2Body   *poBody;
  b2Vec2    vSpeed;
  orxVECTOR *pvResult;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBody != orxNULL);
  orxASSERT(_pvSpeed != orxNULL);

  /* Gets body */
  poBody = (b2Body *)_pstBody;

  /* Gets its speed */
  vSpeed = poBody->GetLinearVelocity();

  /* Updates result */
  pvResult      = _pvSpeed;
  pvResult->fX  = sstPhysics.fRecDimensionRatio * vSpeed.x;
  pvResult->fY  = sstPhysics.fRecDimensionRatio * vSpeed.y;
  pvResult->fZ  = orxFLOAT_0;

  /* Done! */
  return pvResult;
}

extern "C" orxVECTOR *orxFASTCALL orxPhysics_LiquidFun_GetSpeedAtWorldPosition(const orxPHYSICS_BODY *_pstBody, const orxVECTOR *_pvPosition, orxVECTOR *_pvSpeed)
{
  b2Body   *poBody;
  b2Vec2    vSpeed;
  orxVECTOR *pvResult;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBody != orxNULL);
  orxASSERT(_pvPosition != orxNULL);
  orxASSERT(_pvSpeed != orxNULL);

  /* Gets body */
  poBody = (b2Body *)_pstBody;

  /* Gets its speed at given position */
  vSpeed = poBody->GetLinearVelocityFromWorldPoint(b2Vec2(sstPhysics.fDimensionRatio * _pvPosition->fX, sstPhysics.fDimensionRatio * _pvPosition->fY));

  /* Updates result */
  pvResult      = _pvSpeed;
  pvResult->fX  = sstPhysics.fRecDimensionRatio * vSpeed.x;
  pvResult->fY  = sstPhysics.fRecDimensionRatio * vSpeed.y;
  pvResult->fZ  = orxFLOAT_0;

  /* Done! */
  return pvResult;
}

extern "C" orxFLOAT orxFASTCALL orxPhysics_LiquidFun_GetAngularVelocity(const orxPHYSICS_BODY *_pstBody)
{
  b2Body   *poBody;
  orxFLOAT  fResult;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBody != orxNULL);

  /* Gets body */
  poBody = (b2Body *)_pstBody;

  /* Gets its angular velocity */
  fResult = poBody->GetAngularVelocity();

  /* Done! */
  return fResult;
}

extern "C" orxVECTOR *orxFASTCALL orxPhysics_LiquidFun_GetCustomGravity(const orxPHYSICS_BODY *_pstBody, orxVECTOR *_pvCustomGravity)
{
  b2Body       *poBody;
  const b2Vec2 *pvGravity;
  orxVECTOR    *pvResult = orxNULL;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBody != orxNULL);
  orxASSERT(_pvCustomGravity != orxNULL);

  /* Gets body */
  poBody = (b2Body *)_pstBody;

  /* Gets its custom gravity */
  pvGravity = poBody->GetCustomGravity();

  /* Found? */
  if(pvGravity != orxNULL)
  {
    /* Updates result */
    orxVector_Set(_pvCustomGravity, sstPhysics.fRecDimensionRatio * pvGravity->x, sstPhysics.fRecDimensionRatio * pvGravity->y, orxFLOAT_0);
    pvResult = _pvCustomGravity;
  }
  else
  {
    /* Clears result */
    pvResult = orxNULL;
  }

  /* Done! */
  return pvResult;
}

extern "C" orxBOOL orxFASTCALL orxPhysics_LiquidFun_IsFixedRotation(const orxPHYSICS_BODY * _pstBody)
{
  b2Body   *poBody;
  orxBOOL   bResult;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBody != orxNULL);

  /* Gets body */
  poBody = (b2Body *)_pstBody;

  /* Updates result */
  bResult = (poBody->IsFixedRotation() != false) ? orxTRUE : orxFALSE;

  /* Done! */
  return bResult;
}

extern "C" orxFLOAT orxFASTCALL orxPhysics_LiquidFun_GetMass(const orxPHYSICS_BODY *_pstBody)
{
  b2Body     *poBody;
  orxFLOAT    fResult;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBody != orxNULL);

  /* Gets body */
  poBody = (b2Body *)_pstBody;

  /* Updates result */
  fResult = orx2F(poBody->GetMass());

  /* Done! */
  return fResult;
}

extern "C" orxVECTOR *orxFASTCALL orxPhysics_LiquidFun_GetMassCenter(const orxPHYSICS_BODY *_pstBody, orxVECTOR *_pvMassCenter)
{
  b2Body     *poBody;
  b2Vec2      vMassCenter;
  orxVECTOR  *pvResult;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBody != orxNULL);
  orxASSERT(_pvMassCenter != orxNULL);

  /* Gets body */
  poBody = (b2Body *)_pstBody;

  /* Gets its mass center */
  vMassCenter = poBody->GetLocalCenter();

  /* Transfer values */
  _pvMassCenter->fX = sstPhysics.fRecDimensionRatio * vMassCenter.x;
  _pvMassCenter->fY = sstPhysics.fRecDimensionRatio * vMassCenter.y;
  _pvMassCenter->fZ = orxFLOAT_0;

  /* Updates result */
  pvResult = _pvMassCenter;

  /* Done! */
  return pvResult;
}

extern "C" orxSTATUS orxFASTCALL orxPhysics_LiquidFun_SetLinearDamping(orxPHYSICS_BODY *_pstBody, orxFLOAT _fDamping)
{
  b2Body   *poBody;
  orxSTATUS eResult = orxSTATUS_SUCCESS;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBody != orxNULL);

  /* Gets body */
  poBody = (b2Body *)_pstBody;

  /* Sets its linear damping */
  poBody->SetLinearDamping(_fDamping);

  /* Done! */
  return eResult;
}

extern "C" orxSTATUS orxFASTCALL orxPhysics_LiquidFun_SetAngularDamping(orxPHYSICS_BODY *_pstBody, orxFLOAT _fDamping)
{
  b2Body   *poBody;
  orxSTATUS eResult = orxSTATUS_SUCCESS;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBody != orxNULL);

  /* Gets body */
  poBody = (b2Body *)_pstBody;

  /* Sets its angular damping */
  poBody->SetAngularDamping(_fDamping);

  /* Done! */
  return eResult;
}

extern "C" orxFLOAT orxFASTCALL orxPhysics_LiquidFun_GetLinearDamping(const orxPHYSICS_BODY *_pstBody)
{
  b2Body   *poBody;
  orxFLOAT  fResult;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBody != orxNULL);

  /* Gets body */
  poBody = (b2Body *)_pstBody;

  /* Gets its linear damping */
  fResult = orx2F(poBody->GetLinearDamping());

  /* Done! */
  return fResult;
}

extern "C" orxFLOAT orxFASTCALL orxPhysics_LiquidFun_GetAngularDamping(const orxPHYSICS_BODY *_pstBody)
{
  b2Body   *poBody;
  orxFLOAT  fResult;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBody != orxNULL);

  /* Gets body */
  poBody = (b2Body *)_pstBody;

  /* Gets its angular damping */
  fResult = orx2F(poBody->GetAngularDamping());

  /* Done! */
  return fResult;
}

extern "C" orxSTATUS orxFASTCALL orxPhysics_LiquidFun_ApplyTorque(orxPHYSICS_BODY *_pstBody, orxFLOAT _fTorque)
{
  b2Body     *poBody;
  orxOBJECT  *pstObject;
  float32     fTorque = (float32)_fTorque;
  orxSTATUS   eResult = orxSTATUS_SUCCESS;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBody != orxNULL);

  /* Gets body */
  poBody = (b2Body *)_pstBody;

  /* Gets owner object */
  pstObject = orxOBJECT(orxStructure_GetOwner(orxBODY(poBody->GetUserData())));

  /* Is enabled? */
  if((pstObject != orxNULL) && (orxObject_IsEnabled(pstObject) != orxFALSE))
  {
    orxCLOCK *pstClock;

    /* Gets its clock */
    pstClock = orxObject_GetClock(pstObject);

    /* Valid */
    if(pstClock != orxNULL)
    {
      const orxCLOCK_INFO *pstClockInfo;

      /* Gets its info */
      pstClockInfo = orxClock_GetInfo(pstClock);

      /* Has multiplier? */
      if(pstClockInfo->eModType == orxCLOCK_MOD_TYPE_MULTIPLY)
      {
        /* Updates torque */
        fTorque *= (float32)pstClockInfo->fModValue;
      }
    }
  }

  /* Applies torque */
  poBody->ApplyTorque(fTorque, true);

  /* Done! */
  return eResult;
}

extern "C" orxSTATUS orxFASTCALL orxPhysics_LiquidFun_ApplyForce(orxPHYSICS_BODY *_pstBody, const orxVECTOR *_pvForce, const orxVECTOR *_pvPoint)
{
  b2Body     *poBody;
  orxOBJECT  *pstObject;
  b2Vec2      vForce, vPoint;
  orxSTATUS   eResult = orxSTATUS_SUCCESS;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBody != orxNULL);
  orxASSERT(_pvForce != orxNULL);

  /* Sets force */
  vForce.Set(_pvForce->fX, _pvForce->fY);

  /* Gets body */
  poBody = (b2Body *)_pstBody;

  /* Gets owner object */
  pstObject = orxOBJECT(orxStructure_GetOwner(orxBODY(poBody->GetUserData())));

  /* Is enabled? */
  if((pstObject != orxNULL) && (orxObject_IsEnabled(pstObject) != orxFALSE))
  {
    orxCLOCK *pstClock;

    /* Gets its clock */
    pstClock = orxObject_GetClock(pstObject);

    /* Valid */
    if(pstClock != orxNULL)
    {
      const orxCLOCK_INFO *pstClockInfo;

      /* Gets its info */
      pstClockInfo = orxClock_GetInfo(pstClock);

      /* Has multiplier? */
      if(pstClockInfo->eModType == orxCLOCK_MOD_TYPE_MULTIPLY)
      {
        /* Updates force */
        vForce *= (float32)(pstClockInfo->fModValue * pstClockInfo->fModValue);
      }
    }
  }

  /* Has point? */
  if(_pvPoint != orxNULL)
  {
    /* Sets point */
    vPoint.Set(sstPhysics.fDimensionRatio * _pvPoint->fX, sstPhysics.fDimensionRatio * _pvPoint->fY);
  }
  else
  {
    /* Gets world mass center */
    vPoint = poBody->GetWorldCenter();
  }

  /* Applies force */
  poBody->ApplyForce(vForce, vPoint, true);

  /* Done! */
  return eResult;
}

extern "C" orxSTATUS orxFASTCALL orxPhysics_LiquidFun_ApplyImpulse(orxPHYSICS_BODY *_pstBody, const orxVECTOR *_pvImpulse, const orxVECTOR *_pvPoint)
{
  b2Body     *poBody;
  orxOBJECT  *pstObject;
  b2Vec2      vImpulse, vPoint;
  orxSTATUS   eResult = orxSTATUS_SUCCESS;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBody != orxNULL);
  orxASSERT(_pvImpulse != orxNULL);

  /* Sets impulse */
  vImpulse.Set(sstPhysics.fDimensionRatio * _pvImpulse->fX, sstPhysics.fDimensionRatio * _pvImpulse->fY);

  /* Gets body */
  poBody = (b2Body *)_pstBody;

  /* Gets owner object */
  pstObject = orxOBJECT(orxStructure_GetOwner(orxBODY(poBody->GetUserData())));

  /* Is enabled? */
  if((pstObject != orxNULL) && (orxObject_IsEnabled(pstObject) != orxFALSE))
  {
    orxCLOCK *pstClock;

    /* Gets its clock */
    pstClock = orxObject_GetClock(pstObject);

    /* Valid */
    if(pstClock != orxNULL)
    {
      const orxCLOCK_INFO *pstClockInfo;

      /* Gets its info */
      pstClockInfo = orxClock_GetInfo(pstClock);

      /* Has multiplier? */
      if(pstClockInfo->eModType == orxCLOCK_MOD_TYPE_MULTIPLY)
      {
        /* Updates impulse */
        vImpulse *= (float32)pstClockInfo->fModValue;
      }
    }
  }

  /* Has point? */
  if(_pvPoint != orxNULL)
  {
    /* Sets point */
    vPoint.Set(sstPhysics.fDimensionRatio * _pvPoint->fX, sstPhysics.fDimensionRatio * _pvPoint->fY);
  }
  else
  {
    /* Gets world mass center */
    vPoint = poBody->GetWorldCenter();
  }

  /* Applies force */
  poBody->ApplyLinearImpulse(vImpulse, vPoint, true);

  /* Done! */
  return eResult;
}

extern "C" orxSTATUS orxFASTCALL orxPhysics_LiquidFun_SetPartSelfFlags(orxPHYSICS_BODY_PART *_pstBodyPart, orxU16 _u16SelfFlags)
{
  b2Fixture  *poFixture;
  b2Filter    oFilter;
  orxSTATUS   eResult = orxSTATUS_SUCCESS;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBodyPart != orxNULL);

  /* Gets fixture */
  poFixture = (b2Fixture *)_pstBodyPart;

  /* Gets its current filter */
  oFilter = poFixture->GetFilterData();

  /* Updates it */
  oFilter.categoryBits = _u16SelfFlags;

  /* Sets new filter */
  poFixture->SetFilterData(oFilter);

  /* Done! */
  return eResult;
}

extern "C" orxSTATUS orxFASTCALL orxPhysics_LiquidFun_SetPartCheckMask(orxPHYSICS_BODY_PART *_pstBodyPart, orxU16 _u16CheckMask)
{
  b2Fixture  *poFixture;
  b2Filter    oFilter;
  orxSTATUS   eResult = orxSTATUS_SUCCESS;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBodyPart != orxNULL);

  /* Gets fixture */
  poFixture = (b2Fixture *)_pstBodyPart;

  /* Gets its current filter */
  oFilter = poFixture->GetFilterData();

  /* Updates it */
  oFilter.maskBits = _u16CheckMask;

  /* Sets new filter */
  poFixture->SetFilterData(oFilter);

  /* Done! */
  return eResult;
}

extern "C" orxU16 orxFASTCALL orxPhysics_LiquidFun_GetPartSelfFlags(const orxPHYSICS_BODY_PART *_pstBodyPart)
{
  const b2Fixture  *poFixture;
  orxU16            u16Result;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBodyPart != orxNULL);

  /* Gets fixture */
  poFixture = (b2Fixture *)_pstBodyPart;

  /* Updates result */
  u16Result = poFixture->GetFilterData().categoryBits;

  /* Done! */
  return u16Result;
}

extern "C" orxU16 orxFASTCALL orxPhysics_LiquidFun_GetPartCheckMask(const orxPHYSICS_BODY_PART *_pstBodyPart)
{
  const b2Fixture  *poFixture;
  orxU16            u16Result;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBodyPart != orxNULL);

  /* Gets fixture */
  poFixture = (b2Fixture *)_pstBodyPart;

  /* Updates result */
  u16Result = poFixture->GetFilterData().maskBits;

  /* Done! */
  return u16Result;
}

extern "C" orxBOOL orxFASTCALL orxPhysics_LiquidFun_IsPartSolid(const orxPHYSICS_BODY_PART *_pstBodyPart)
{
  b2Fixture  *poFixture;
  orxBOOL     bResult;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBodyPart != orxNULL);

  /* Gets fixture */
  poFixture = (b2Fixture *)_pstBodyPart;

  /* Updates it */
  bResult = poFixture->IsSensor() ? orxFALSE : orxTRUE;

  /* Done! */
  return bResult;
}

extern "C" orxSTATUS orxFASTCALL orxPhysics_LiquidFun_SetPartSolid(orxPHYSICS_BODY_PART *_pstBodyPart, orxBOOL _bSolid)
{
  b2Fixture  *poFixture;
  orxSTATUS   eResult = orxSTATUS_SUCCESS;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pstBodyPart != orxNULL);

  /* Gets fixture */
  poFixture = (b2Fixture *)_pstBodyPart;

  /* Updates it */
  poFixture->SetSensor((_bSolid != orxFALSE) ? false : true);

  /* Done! */
  return eResult;
}

extern "C" orxHANDLE orxFASTCALL orxPhysics_LiquidFun_Raycast(const orxVECTOR *_pvStart, const orxVECTOR *_pvEnd, orxU16 _u16SelfFlags, orxU16 _u16CheckMask, orxBOOL _bEarlyExit, orxVECTOR *_pvContact, orxVECTOR *_pvNormal)
{
  b2Vec2          vStart, vEnd;
  RayCastCallback oRaycastCallback;
  orxHANDLE       hResult = orxHANDLE_UNDEFINED;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pvStart != orxNULL);
  orxASSERT(_pvEnd != orxNULL);

  /* Gets extremities */
  vStart.Set(sstPhysics.fDimensionRatio * _pvStart->fX, sstPhysics.fDimensionRatio * _pvStart->fY);
  vEnd.Set(sstPhysics.fDimensionRatio * _pvEnd->fX, sstPhysics.fDimensionRatio * _pvEnd->fY);

  /* Inits filter data */
  oRaycastCallback.u16SelfFlags = _u16SelfFlags;
  oRaycastCallback.u16CheckMask = _u16CheckMask;

  /* Stores early exit status */
  oRaycastCallback.bEarlyExit   = _bEarlyExit;

  /* Issues Raycast */
  sstPhysics.poWorld->RayCast(&oRaycastCallback, vStart, vEnd);

  /* Found? */
  if(oRaycastCallback.hResult != orxHANDLE_UNDEFINED)
  {
    /* Updates result */
    hResult = oRaycastCallback.hResult;

    /* Asked for contact? */
    if(_pvContact != orxNULL)
    {
      /* Updates it */
      orxVector_Set(_pvContact, oRaycastCallback.vContact.fX, oRaycastCallback.vContact.fY, _pvStart->fZ);
    }

    /* Asked for normal? */
    if(_pvNormal != orxNULL)
    {
      /* Updates it */
      orxVector_Copy(_pvNormal, &(oRaycastCallback.vNormal));
    }
  }

  /* Done! */
  return hResult;
}

extern "C" orxPHYSICS_PARTICLEGROUP *orxFASTCALL orxPhysics_LiquidFun_CreateParticleGroup(const orxHANDLE _hUserData, const orxPARTICLEGROUP_DEF *_pstParticleGroupDef)
{
  b2ParticleGroup     *poResult = 0;
  b2ParticleGroupDef   stParticleGroupDef;
  b2ParticleSystem    *poParticleSystem;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_hUserData != orxHANDLE_UNDEFINED);
  orxASSERT(_pstParticleGroupDef != orxNULL);

  /* 2D? */
  if(orxFLAG_TEST(_pstParticleGroupDef->u32Flags, orxPARTICLEGROUP_DEF_KU32_FLAG_2D))
  {
    /* Inits particle group definition */
    stParticleGroupDef.userData          = _hUserData;
    b2Shape** shapes                     = orxNULL;

    if(orxFLAG_TEST(_pstParticleGroupDef->u32Flags, orxPARTICLEGROUP_DEF_KU32_FLAG_SOLID))
    {
      stParticleGroupDef.groupFlags |= b2_solidParticleGroup;
    }
    if(orxFLAG_TEST(_pstParticleGroupDef->u32Flags, orxPARTICLEGROUP_DEF_KU32_FLAG_RIGID))
    {
      stParticleGroupDef.groupFlags |= b2_rigidParticleGroup;
    }
    if(orxFLAG_TEST(_pstParticleGroupDef->u32Flags, orxPARTICLEGROUP_DEF_KU32_FLAG_CAN_BE_EMPTY))
    {
      stParticleGroupDef.groupFlags |= b2_particleGroupCanBeEmpty;
    }
    if(_pstParticleGroupDef->s32ShapeCount > 0)
    {
      shapes = new b2Shape*[_pstParticleGroupDef->s32ShapeCount];
      stParticleGroupDef.shapeCount = _pstParticleGroupDef->s32ShapeCount;
      stParticleGroupDef.shapes = (const b2Shape* const*)shapes;

      for(int i = 0; i < _pstParticleGroupDef->s32ShapeCount; i++)
      {
        const orxPARTICLEGROUP_SHAPE_DEF *pstShapeDef = _pstParticleGroupDef->apstShapesDef[i];

        /* Circle? */
        if(orxFLAG_TEST(pstShapeDef->u32Flags, orxPARTICLEGROUP_SHAPE_DEF_KU32_FLAG_SPHERE))
        {
          b2CircleShape *pstCircleShape = new b2CircleShape();

          /* Stores shape reference */
          shapes[i] = pstCircleShape;

          /* Stores its coordinates */
          pstCircleShape->m_p.Set(sstPhysics.fDimensionRatio * pstShapeDef->stSphere.vCenter.fX, sstPhysics.fDimensionRatio * pstShapeDef->stSphere.vCenter.fY);
          pstCircleShape->m_radius = sstPhysics.fDimensionRatio * pstShapeDef->stSphere.fRadius;
        }
        /* Polygon */
        else
        {
          b2PolygonShape  *pstPolygonShape = new b2PolygonShape();

          /* Stores shape reference */
          shapes[i] = pstPolygonShape;

          /* Box? */
          if(orxFLAG_TEST(pstShapeDef->u32Flags, orxPARTICLEGROUP_SHAPE_DEF_KU32_FLAG_BOX))
          {
            b2Vec2 avVertexList[4];

            /* Stores its coordinates */
            avVertexList[0].Set(sstPhysics.fDimensionRatio * pstShapeDef->stAABox.stBox.vTL.fX, sstPhysics.fDimensionRatio * pstShapeDef->stAABox.stBox.vTL.fY);
            avVertexList[1].Set(sstPhysics.fDimensionRatio * pstShapeDef->stAABox.stBox.vTL.fX, sstPhysics.fDimensionRatio * pstShapeDef->stAABox.stBox.vBR.fY);
            avVertexList[2].Set(sstPhysics.fDimensionRatio * pstShapeDef->stAABox.stBox.vBR.fX, sstPhysics.fDimensionRatio * pstShapeDef->stAABox.stBox.vBR.fY);
            avVertexList[3].Set(sstPhysics.fDimensionRatio * pstShapeDef->stAABox.stBox.vBR.fX, sstPhysics.fDimensionRatio * pstShapeDef->stAABox.stBox.vTL.fY);

            /* Updates shape */
            pstPolygonShape->Set(avVertexList, 4);
          }
          else
          {
            b2Vec2 avVertexList[b2_maxPolygonVertices];
            orxU32 i;

            /* Checks */
            orxASSERT(pstShapeDef->stMesh.u32VertexCounter > 0);
            orxASSERT(orxBODY_PART_DEF_KU32_MESH_VERTEX_NUMBER <= b2_maxPolygonVertices);

            /* For all the vertices */
            for(i = 0; i < pstShapeDef->stMesh.u32VertexCounter; i++)
            {
              /* Sets its vector */
              avVertexList[i].Set(sstPhysics.fDimensionRatio * pstShapeDef->stMesh.avVertices[i].fX, sstPhysics.fDimensionRatio * pstShapeDef->stMesh.avVertices[i].fY);
            }

            /* Updates shape */
            pstPolygonShape->Set(avVertexList, (int32)pstShapeDef->stMesh.u32VertexCounter);
          }
        }
      }
    }

    // TODO stParticleGroupDef.flags
    // TODO stParticleGroupDef.color
    // TODO stParticleGroupDef.lifetime
    // TODO stParticleGroupDef.position
    // TODO stParticleGroupDef.angle
    // TODO stParticleGroupDef.linearVelocity
    // TODO stParticleGroupDef.angularVelocity
    // TODO stParticleGroupDef.strength

    /* Merge with existing group ? */
    if(_pstParticleGroupDef->hParticleGroup != orxHANDLE_UNDEFINED)
    {
      stParticleGroupDef.group = (b2ParticleGroup*) _pstParticleGroupDef->hParticleGroup;
      poParticleSystem = stParticleGroupDef.group->GetParticleSystem();
    }
    else
    {
      orxASSERT(_pstParticleGroupDef->zParticleSystemName != orxNULL);
      poParticleSystem = (b2ParticleSystem*) orxHashTable_Get(sstPhysics.pstParticleSystems, orxString_GetID(_pstParticleGroupDef->zParticleSystemName));
    }

    /* Creates particle group */
    poResult = poParticleSystem->CreateParticleGroup(stParticleGroupDef);

    /* free memory */
    for(int i = 0; i < stParticleGroupDef.shapeCount; i++)
    {
      delete shapes[i];
    }

    if(shapes != orxNULL)
    {
      delete[] shapes;
    }
  }

  /* Done! */
  return (orxPHYSICS_PARTICLEGROUP *)poResult;
}

extern "C" void orxFASTCALL orxPhysics_LiquidFun_EnableSimulation(orxBOOL _bEnable)
{
  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);

  /* Enabled? */
  if(_bEnable != orxFALSE)
  {
    /* Updates status */
    orxFLAG_SET(sstPhysics.u32Flags, orxPHYSICS_KU32_STATIC_FLAG_ENABLED, orxPHYSICS_KU32_STATIC_FLAG_NONE);
  }
  else
  {
    /* Updates status */
    orxFLAG_SET(sstPhysics.u32Flags, orxPHYSICS_KU32_STATIC_FLAG_NONE, orxPHYSICS_KU32_STATIC_FLAG_ENABLED);
  }

  /* Done! */
  return;
}

extern "C" orxSTATUS orxFASTCALL orxPhysics_LiquidFun_SetGravity(const orxVECTOR *_pvGravity)
{
  b2Vec2    vGravity;
  orxSTATUS eResult = orxSTATUS_SUCCESS;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pvGravity != orxNULL);

  /* Sets gravity vector */
  vGravity.Set(sstPhysics.fDimensionRatio * _pvGravity->fX, sstPhysics.fDimensionRatio * _pvGravity->fY);

  /* Updates gravity */
  sstPhysics.poWorld->SetGravity(vGravity);

  /* Done! */
  return eResult;
}

extern "C" orxVECTOR *orxFASTCALL orxPhysics_LiquidFun_GetGravity(orxVECTOR *_pvGravity)
{
  b2Vec2      vGravity;
  orxVECTOR  *pvResult = _pvGravity;

  /* Checks */
  orxASSERT(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY);
  orxASSERT(_pvGravity != orxNULL);

  /* Gets gravity vector */
  vGravity = sstPhysics.poWorld->GetGravity();
  orxVector_Set(_pvGravity, sstPhysics.fRecDimensionRatio * vGravity.x, sstPhysics.fRecDimensionRatio * vGravity.y, orxFLOAT_0);

  /* Done! */
  return pvResult;
}

extern "C" orxSTATUS orxFASTCALL orxPhysics_LiquidFun_Init()
{
  orxSTATUS eResult = orxSTATUS_SUCCESS;

  /* Was not already initialized? */
  if(!(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY))
  {
    orxFLOAT  fRatio;
    orxU32    u32FixedTimeStepRate;
    orxVECTOR vGravity;
    b2Vec2    vWorldGravity;

    /* Cleans static controller */
    orxMemory_Zero(&sstPhysics, sizeof(orxPHYSICS_STATIC));

    /* Pushes config section */
    orxConfig_PushSection(orxPHYSICS_KZ_CONFIG_SECTION);

    /* Sets custom memory alloc/free */
    b2SetAllocFreeCallbacks(orxPhysics_LiquidFun_Allocate, orxPhysics_LiquidFun_Free, NULL);

    /* Gets gravity & allow sleep from config */
    if(orxConfig_GetVector(orxPHYSICS_KZ_CONFIG_GRAVITY, &vGravity) == orxNULL)
    {
      orxVector_Copy(&vGravity, &orxVECTOR_0);
    }

    /* Gets fixed timestep rate */
    u32FixedTimeStepRate = orxConfig_GetU32(orxPHYSICS_KZ_CONFIG_FIXED_TIMESTEP_RATE);

    /* Valid? */
    if(u32FixedTimeStepRate > 0)
    {
      /* Sotres it */
      sstPhysics.fFixedTimeStep = orxFLOAT_1 / orx2F(u32FixedTimeStepRate);
    }

    /* Gets dimension ratio */
    fRatio = orxConfig_GetFloat(orxPHYSICS_KZ_CONFIG_RATIO);

    /* Valid? */
    if(fRatio > orxFLOAT_0)
    {
      /* Stores it */
      sstPhysics.fDimensionRatio = fRatio;
    }
    else
    {
      /* Stores default one */
      sstPhysics.fDimensionRatio = orxPhysics::sfDefaultDimensionRatio;
    }

    /* Inits world gravity */
    vWorldGravity.Set(sstPhysics.fDimensionRatio * vGravity.fX, sstPhysics.fDimensionRatio * vGravity.fY);

    /* Creates world */
    sstPhysics.poWorld = new b2World(vWorldGravity);

    /* Success? */
    if(sstPhysics.poWorld != orxNULL)
    {
      orxCLOCK *pstClock;
      orxU32    u32IterationsPerStep, u32ParticleIterationsPerStep;

      sstPhysics.pstParticleSystems = orxHashTable_Create(orxPHYSICS_KU32_PARTICLE_SYSTEM_HASHTABLE_SIZE, orxHASHTABLE_KU32_FLAG_NONE, orxMEMORY_TYPE_PHYSICS);

      /* Success ? */
      if(sstPhysics.pstParticleSystems != orxNULL)
      {
        /* Create Particle Systems */
        for(orxS32 i = 0; i < orxConfig_GetListCounter(orxPHYSICS_KZ_CONFIG_PARTICLE_SYSTEM_LIST); i++)
        {
          const orxSTRING zParticleSystemName = orxConfig_GetListString(orxPHYSICS_KZ_CONFIG_PARTICLE_SYSTEM_LIST, i);
          b2ParticleSystemDef particleSystemDef;

          if(orxConfig_HasSection(zParticleSystemName))
          {
            orxConfig_PushSection(zParticleSystemName);

            if(orxConfig_HasValue(orxPHYSICS_KZ_CONFIG_RADIUS))
            {
              particleSystemDef.radius = orxConfig_GetFloat(orxPHYSICS_KZ_CONFIG_RADIUS);
            }
            if(orxConfig_HasValue(orxPHYSICS_KZ_CONFIG_DENSITY))
            {
              particleSystemDef.density = orxConfig_GetFloat(orxPHYSICS_KZ_CONFIG_DENSITY);
            }
            if(orxConfig_HasValue(orxPHYSICS_KZ_CONFIG_MAX_PARTICLE_COUNT))
            {
              particleSystemDef.maxCount = orxConfig_GetS32(orxPHYSICS_KZ_CONFIG_MAX_PARTICLE_COUNT);
            }
            if(orxConfig_HasValue(orxPHYSICS_KZ_CONFIG_PRESSURE_STRENGTH))
            {
              particleSystemDef.pressureStrength = orxConfig_GetFloat(orxPHYSICS_KZ_CONFIG_PRESSURE_STRENGTH);
            }
            if(orxConfig_HasValue(orxPHYSICS_KZ_CONFIG_DAMPING_STRENGTH))
            {
              particleSystemDef.dampingStrength = orxConfig_GetFloat(orxPHYSICS_KZ_CONFIG_DAMPING_STRENGTH);
            }
            if(orxConfig_HasValue(orxPHYSICS_KZ_CONFIG_ELASTIC_STRENGTH))
            {
              particleSystemDef.elasticStrength = orxConfig_GetFloat(orxPHYSICS_KZ_CONFIG_ELASTIC_STRENGTH);
            }
            if(orxConfig_HasValue(orxPHYSICS_KZ_CONFIG_SPRING_STRENGTH))
            {
              particleSystemDef.springStrength = orxConfig_GetFloat(orxPHYSICS_KZ_CONFIG_SPRING_STRENGTH);
            }
            if(orxConfig_HasValue(orxPHYSICS_KZ_CONFIG_VISCOUS_STRENGTH))
            {
              particleSystemDef.viscousStrength = orxConfig_GetFloat(orxPHYSICS_KZ_CONFIG_VISCOUS_STRENGTH);
            }

            orxConfig_PopSection();
          }

          /* Create Particle System */
          b2ParticleSystem *poParticleSystem = sstPhysics.poWorld->CreateParticleSystem(&particleSystemDef);

          /* Success ? */
          if(poParticleSystem != orxNULL)
          {
            orxHashTable_Add(sstPhysics.pstParticleSystems, orxString_GetID(zParticleSystemName), (void*)poParticleSystem);
          }
        }
      }

      /* Creates listeners */
      sstPhysics.poContactListener = new orxPhysicsContactListener();

      /* Registers them */
      sstPhysics.poWorld->SetContactListener(sstPhysics.poContactListener);

      /* Removes auto clear forces */
      sstPhysics.poWorld->SetAutoClearForces(false);

      /* Stores inverse dimension ratio */
      sstPhysics.fRecDimensionRatio = orxFLOAT_1 / sstPhysics.fDimensionRatio;

      /* Gets iteration per step number from config */
      u32IterationsPerStep = orxConfig_GetU32(orxPHYSICS_KZ_CONFIG_ITERATIONS);

      /* Valid? */
      if(u32IterationsPerStep > 0)
      {
        /* Stores it */
        sstPhysics.u32Iterations = u32IterationsPerStep;
      }
      else
      {
        /* Uses default value */
        sstPhysics.u32Iterations = orxPhysics::su32DefaultIterations;
      }

      /* Gets particle iterations per step number from config */
      u32ParticleIterationsPerStep = orxConfig_GetU32(orxPHYSICS_KZ_CONFIG_PARTICLE_ITERATIONS);

      /* Valid? */
      if(u32ParticleIterationsPerStep > 0)
      {
        /* Stores it */
        sstPhysics.u32ParticleIterations = u32ParticleIterationsPerStep;
      }
      else
      {
        /* Uses default value */
        orxFLOAT fGravity = sstPhysics.poWorld->GetGravity().Length();
        sstPhysics.u32ParticleIterations =  b2CalculateParticleIterations(fGravity, orxPhysics::sfSmallesParticleRadius, sstPhysics.fFixedTimeStep ? sstPhysics.fFixedTimeStep : orxPhysics::sfMaxDT);
      }

      /* Gets core clock */
      pstClock = orxClock_FindFirst(orx2F(-1.0f), orxCLOCK_TYPE_CORE);

      /* Resyncs clocks */
      orxClock_ResyncAll();

      /* Valid? */
      if(pstClock != orxNULL)
      {
        /* Registers rendering function */
        eResult = orxClock_Register(pstClock, orxPhysics_LiquidFun_Update, orxNULL, orxMODULE_ID_PHYSICS, orxCLOCK_PRIORITY_LOWER);

        /* Valid? */
        if(eResult != orxSTATUS_FAILURE)
        {
          /* Creates event bank */
          sstPhysics.pstEventBank = orxBank_Create(orxPhysics::su32MessageBankSize, sizeof(orxPHYSICS_EVENT_STORAGE), orxBANK_KU32_FLAG_NONE, orxMEMORY_TYPE_MAIN);

#ifdef orxPHYSICS_ENABLE_DEBUG_DRAW

          /* Creates debug draw */
          sstPhysics.poDebugDraw = new orxPhysicsDebugDraw();

          /* Inits it */
          sstPhysics.poDebugDraw->SetFlags(b2Draw::e_shapeBit | b2Draw::e_jointBit | b2Draw::e_centerOfMassBit | b2Draw::e_particleBit);

          /* Registers it */
          sstPhysics.poWorld->SetDebugDraw(sstPhysics.poDebugDraw);

          /* Adds event handler */
          orxEvent_AddHandler(orxEVENT_TYPE_RENDER, orxPhysics_LiquidFun_EventHandler);

#endif /* orxPHYSICS_ENABLE_DEBUG_DRAW */

          /* Updates status */
          sstPhysics.u32Flags |= orxPHYSICS_KU32_STATIC_FLAG_READY | orxPHYSICS_KU32_STATIC_FLAG_ENABLED;
        }
        else
        {
          /* Deletes listeners */
          delete sstPhysics.poContactListener;

          /* Deletes particle systems */
          if(sstPhysics.pstParticleSystems != orxNULL)
          {
            orxHANDLE iterator;
            b2ParticleSystem *poParticleSystem;

            for(iterator = orxHashTable_GetNext(sstPhysics.pstParticleSystems, orxNULL, orxNULL, (void**)&poParticleSystem);
              iterator != orxHANDLE_UNDEFINED;
              iterator = orxHashTable_GetNext(sstPhysics.pstParticleSystems, iterator, orxNULL, (void**)&poParticleSystem))
            {
              sstPhysics.poWorld->DestroyParticleSystem(poParticleSystem);
            }

            orxHashTable_Delete(sstPhysics.pstParticleSystems);
          }

          /* Deletes world */
          delete sstPhysics.poWorld;

          /* Updates result */
          eResult = orxSTATUS_FAILURE;
        }
      }
      else
      {
        /* Deletes listeners */
        delete sstPhysics.poContactListener;

        /* Deletes particle systems */
        if(sstPhysics.pstParticleSystems != orxNULL)
        {
          orxHANDLE iterator;
          b2ParticleSystem *poParticleSystem;

          for(iterator = orxHashTable_GetNext(sstPhysics.pstParticleSystems, orxNULL, orxNULL, (void**)&poParticleSystem);
            iterator != orxHANDLE_UNDEFINED;
            iterator = orxHashTable_GetNext(sstPhysics.pstParticleSystems, iterator, orxNULL, (void**)&poParticleSystem))
          {
            sstPhysics.poWorld->DestroyParticleSystem(poParticleSystem);
          }

          orxHashTable_Delete(sstPhysics.pstParticleSystems);
        }

        /* Deletes world */
        delete sstPhysics.poWorld;

        /* Updates result */
        eResult = orxSTATUS_FAILURE;
      }
    }
    else
    {
      /* Updates result */
      eResult = orxSTATUS_FAILURE;
    }

    /* Pops config section */
    orxConfig_PopSection();
  }

  /* Done! */
  return eResult;
}

extern "C" void orxFASTCALL orxPhysics_LiquidFun_Exit()
{
  /* Was initialized? */
  if(sstPhysics.u32Flags & orxPHYSICS_KU32_STATIC_FLAG_READY)
  {

    /* Deletes the particle systems */
    if(sstPhysics.pstParticleSystems != orxNULL)
    {
      orxHANDLE iterator;
      b2ParticleSystem *poParticleSystem;

      for(iterator = orxHashTable_GetNext(sstPhysics.pstParticleSystems, orxNULL, orxNULL, (void**)&poParticleSystem);
        iterator != orxHANDLE_UNDEFINED;
        iterator = orxHashTable_GetNext(sstPhysics.pstParticleSystems, iterator, orxNULL, (void**)&poParticleSystem))
      {
        sstPhysics.poWorld->DestroyParticleSystem(poParticleSystem);
      }

      orxHashTable_Delete(sstPhysics.pstParticleSystems);
    }

    /* Deletes the listeners */
    delete sstPhysics.poContactListener;

#ifdef orxPHYSICS_ENABLE_DEBUG_DRAW

    /* Removes event handler */
    orxEvent_RemoveHandler(orxEVENT_TYPE_RENDER, orxPhysics_LiquidFun_EventHandler);

    /* Deletes debug draw */
    delete sstPhysics.poDebugDraw;

#endif /* orxPHYSICS_ENABLE_DEBUG_DRAW */

    /* Deletes world */
    delete sstPhysics.poWorld;

    /* Cleans static controller */
    orxMemory_Zero(&sstPhysics, sizeof(orxPHYSICS_STATIC));
  }

  return;
}


/***************************************************************************
 * Plugin related                                                          *
 ***************************************************************************/

orxPLUGIN_USER_CORE_FUNCTION_START(PHYSICS);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_Init, PHYSICS, INIT);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_Exit, PHYSICS, EXIT);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_SetGravity, PHYSICS, SET_GRAVITY);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_GetGravity, PHYSICS, GET_GRAVITY);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_CreateBody, PHYSICS, CREATE_BODY);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_DeleteBody, PHYSICS, DELETE_BODY);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_CreatePart, PHYSICS, CREATE_PART);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_DeletePart, PHYSICS, DELETE_PART);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_CreateJoint, PHYSICS, CREATE_JOINT);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_DeleteJoint, PHYSICS, DELETE_JOINT);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_SetPosition, PHYSICS, SET_POSITION);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_SetRotation, PHYSICS, SET_ROTATION);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_SetSpeed, PHYSICS, SET_SPEED);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_SetAngularVelocity, PHYSICS, SET_ANGULAR_VELOCITY);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_SetCustomGravity, PHYSICS, SET_CUSTOM_GRAVITY);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_SetFixedRotation, PHYSICS, SET_FIXED_ROTATION);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_GetPosition, PHYSICS, GET_POSITION);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_GetRotation, PHYSICS, GET_ROTATION);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_GetSpeed, PHYSICS, GET_SPEED);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_GetSpeedAtWorldPosition, PHYSICS, GET_SPEED_AT_WORLD_POSITION);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_GetAngularVelocity, PHYSICS, GET_ANGULAR_VELOCITY);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_GetCustomGravity, PHYSICS, GET_CUSTOM_GRAVITY);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_IsFixedRotation, PHYSICS, IS_FIXED_ROTATION);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_GetMass, PHYSICS, GET_MASS);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_GetMassCenter, PHYSICS, GET_MASS_CENTER);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_SetLinearDamping, PHYSICS, SET_LINEAR_DAMPING);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_SetAngularDamping, PHYSICS, SET_ANGULAR_DAMPING);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_GetLinearDamping, PHYSICS, GET_LINEAR_DAMPING);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_GetAngularDamping, PHYSICS, GET_ANGULAR_DAMPING);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_ApplyTorque, PHYSICS, APPLY_TORQUE);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_ApplyForce, PHYSICS, APPLY_FORCE);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_ApplyImpulse, PHYSICS, APPLY_IMPULSE);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_SetPartSelfFlags, PHYSICS, SET_PART_SELF_FLAGS);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_SetPartCheckMask, PHYSICS, SET_PART_CHECK_MASK);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_GetPartSelfFlags, PHYSICS, GET_PART_SELF_FLAGS);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_GetPartCheckMask, PHYSICS, GET_PART_CHECK_MASK);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_IsPartSolid, PHYSICS, IS_PART_SOLID);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_SetPartSolid, PHYSICS, SET_PART_SOLID);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_EnableMotor, PHYSICS, ENABLE_MOTOR);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_SetJointMotorSpeed, PHYSICS, SET_JOINT_MOTOR_SPEED);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_SetJointMaxMotorTorque, PHYSICS, SET_JOINT_MAX_MOTOR_TORQUE);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_GetJointReactionForce, PHYSICS, GET_JOINT_REACTION_FORCE);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_GetJointReactionTorque, PHYSICS, GET_JOINT_REACTION_TORQUE);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_Raycast, PHYSICS, RAYCAST);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_CreateParticleGroup, PHYSICS, CREATE_PARTICLE_GROUP);
orxPLUGIN_USER_CORE_FUNCTION_ADD(orxPhysics_LiquidFun_EnableSimulation, PHYSICS, ENABLE_SIMULATION);
orxPLUGIN_USER_CORE_FUNCTION_END();


#ifdef __orxMSVC__

  #pragma warning(default : 4311 4312)

#endif /* __orxMSVC__ */