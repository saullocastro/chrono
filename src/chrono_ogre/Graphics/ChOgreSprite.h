/*
Author: Charles Ricchio

ChOgreSprite contains and maintains a shell over the Ogre::Billboard so that images
can be created freely throughout the scene
*/

#pragma once

#include "chrono_ogre/ChOgreApi.h"
#include <Ogre.h>

namespace chrono {
	namespace ChOgre {

		class CHOGRE_DLL_TAG ChOgreSprite {

		public:

			ChOgreSprite(Ogre::BillboardSet* Billboardset);
			~ChOgreSprite();



		protected:

			Ogre::BillboardSet* m_pBillboardSet;
			Ogre::Billboard* m_pBillboard;
		};

	}
}