/**
  src/library_entry_point.c

  The library entry point. It must have the same name for each library
  of the components loaded by the ST static component loader.  This
  function fills the version, the component name and if existing also
  the roles and the specific names for each role. This base function
  is only an explanation.  For each library it must be implemented,
  and it must fill data of any component in the library

  Copyright (C) 2007-2009 STMicroelectronics
  Copyright (C) 2007-2009 Nokia Corporation and/or its subsidiary(-ies).

  This library is free software; you can redistribute it and/or modify it under
  the terms of the GNU Lesser General Public License as published by the Free
  Software Foundation; either version 2.1 of the License, or (at your option)
  any later version.

  This library is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
  FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
  details.

  You should have received a copy of the GNU Lesser General Public License
  along with this library; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St, Fifth Floor, Boston, MA
  02110-1301  USA

*/

#include <stdlib.h>
#include <string.h>

#include <bellagio/st_static_component_loader.h>

#define NUM_COMPONENTS (sizeof(stComponents) / sizeof(stComponent))

extern OMX_ERRORTYPE shvpu_avcdec_Constructor(OMX_COMPONENTTYPE*, OMX_STRING);

typedef struct {
  char* role;
  char *name;
  char *name_specific;
  OMX_ERRORTYPE (*constructor)(OMX_COMPONENTTYPE* ,OMX_STRING);
} stComponent;

stComponent stComponents[] = {
  /* standard components - no functionality */
  { "video_decoder.avc",   "OMX.re.video_decoder",
    "OMX.re.video_decoder.avc",    shvpu_avcdec_Constructor  },
};

static OMX_ERRORTYPE add_component(
  stLoaderComponentType* pst,
  char* role,
  char* name,
  char* name_specific,
  OMX_ERRORTYPE (*constructor)(OMX_COMPONENTTYPE* ,OMX_STRING))
{
  unsigned int i;

  pst->componentVersion.s.nVersionMajor = 1;
  pst->componentVersion.s.nVersionMinor = 1;
  pst->componentVersion.s.nRevision = 1;
  pst->componentVersion.s.nStep = 1;

  pst->name = calloc(1, OMX_MAX_STRINGNAME_SIZE);
  if (pst->name == NULL) {
    return OMX_ErrorInsufficientResources;
  }
  strcpy(pst->name, name);
  pst->name_specific_length = 1;
  pst->constructor = constructor;

  pst->name_specific = calloc(pst->name_specific_length, sizeof(char *));
  pst->role_specific = calloc(pst->name_specific_length, sizeof(char *));

  for(i=0;i<pst->name_specific_length;i++) {
    pst->name_specific[i] = calloc(1, OMX_MAX_STRINGNAME_SIZE);
    if (pst->name_specific[i] == NULL) {
      return OMX_ErrorInsufficientResources;
    }
  }

  for(i=0;i<pst->name_specific_length;i++) {
    pst->role_specific[i] = calloc(1, OMX_MAX_STRINGNAME_SIZE);
    if (pst->role_specific[i] == NULL) {
      return OMX_ErrorInsufficientResources;
    }
  }

  strcpy(pst->name_specific[0], name_specific);
  strcpy(pst->role_specific[0], role);

  return OMX_ErrorNone;
}

/** @brief The library entry point. It must have the same name for each
  * library fo the components loaded by the ST static component loader.
  *
  * This function fills the version, the component name and if existing also the roles
  * and the specific names for each role. This base function is only an explanation.
  * For each library it must be implemented, and it must fill data of any component
  * in the library
  *
  * @param stComponents pointer to an array of components descriptors.If NULL, the
  * function will return only the number of components contained in the library
  *
  * @return number of components contained in the library
  */
int omx_component_library_Setup(stLoaderComponentType **stLoader)
{
  int i;

  DEBUG(DEB_LEV_FUNCTION_NAME, "In %s \n",__func__);

  if (stLoader == NULL) {
    DEBUG(DEB_LEV_FUNCTION_NAME, "Out of %s \n",__func__);
    return NUM_COMPONENTS; // Return Number of Components
  }

  for(i = 0; i < sizeof(stComponents) / sizeof(stComponent); i++)
    add_component(stLoader[i],
		  stComponents[i].role,
		  stComponents[i].name,
		  stComponents[i].name_specific,
		  stComponents[i].constructor);

  DEBUG(DEB_LEV_FUNCTION_NAME, "Out of %s \n",__func__);

  return NUM_COMPONENTS;
}

