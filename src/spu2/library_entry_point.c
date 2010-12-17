/**
  src/spu2/library_entry_point.c

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

#include <bellagio/st_static_component_loader.h>
#include "omx_audiodec_component.h"
#include "omx_audioenc_component.h"

int
omx_component_library_Setup (stLoaderComponentType **stComponents)
{
	OMX_U32 i = 0;
	stLoaderComponentType tmp1, *tmp2[2], *p;

	if (stComponents == NULL) {
		tmp2[0] = &tmp1;
		tmp2[1] = &tmp1;
		stComponents = tmp2;
	}
#ifdef LIBSPUHELPERAACDEC
	p = stComponents[i++];
	/** component 1 - audio decoder */
	p->componentVersion.s.nVersionMajor = 1;
	p->componentVersion.s.nVersionMinor = 1;
	p->componentVersion.s.nRevision = 1;
	p->componentVersion.s.nStep = 1;

	p->name = calloc (1, OMX_MAX_STRINGNAME_SIZE);
	if (p->name == NULL)
		return OMX_ErrorInsufficientResources;
	strcpy (p->name, "OMX.re.audio_decoder");
	p->name_specific_length = 1;
	p->constructor = omx_audiodec_component_Constructor;

	p->name_specific = calloc (1, sizeof p->name_specific[0]);
	if (p->name_specific == NULL)
		return OMX_ErrorInsufficientResources;
	p->role_specific = calloc (1, sizeof p->role_specific[0]);
	if (p->role_specific == NULL)
		return OMX_ErrorInsufficientResources;

	p->name_specific[0] = calloc (1, OMX_MAX_STRINGNAME_SIZE);
	if (p->name_specific[0] == NULL)
		return OMX_ErrorInsufficientResources;
	p->role_specific[0] = calloc (1, OMX_MAX_STRINGNAME_SIZE);
	if (p->role_specific[0] == NULL)
		return OMX_ErrorInsufficientResources;

	strcpy (p->name_specific[0], "OMX.re.audio_decoder.aac");
	strcpy (p->role_specific[0], "audio_decoder.aac");
#endif

#ifdef LIBSPUHELPERAACENC
	p = stComponents[i++];
	/** component 1 - audio decoder */
	p->componentVersion.s.nVersionMajor = 1;
	p->componentVersion.s.nVersionMinor = 1;
	p->componentVersion.s.nRevision = 1;
	p->componentVersion.s.nStep = 1;

	p->name = calloc (1, OMX_MAX_STRINGNAME_SIZE);
	if (p->name == NULL)
		return OMX_ErrorInsufficientResources;
	strcpy (p->name, "OMX.re.audio_encoder");
	p->name_specific_length = 1;
	p->constructor = omx_audioenc_component_Constructor;

	p->name_specific = calloc (1, sizeof p->name_specific[0]);
	if (p->name_specific == NULL)
		return OMX_ErrorInsufficientResources;
	p->role_specific = calloc (1, sizeof p->role_specific[0]);
	if (p->role_specific == NULL)
		return OMX_ErrorInsufficientResources;

	p->name_specific[0] = calloc (1, OMX_MAX_STRINGNAME_SIZE);
	if (p->name_specific[0] == NULL)
		return OMX_ErrorInsufficientResources;
	p->role_specific[0] = calloc (1, OMX_MAX_STRINGNAME_SIZE);
	if (p->role_specific[0] == NULL)
		return OMX_ErrorInsufficientResources;

	strcpy (p->name_specific[0], "OMX.re.audio_encoder.aac");
	strcpy (p->role_specific[0], "audio_encoder.aac");
#endif

	return i;
}
