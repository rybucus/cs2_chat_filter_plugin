#pragma once
#include <entity2/entityidentity.h>
#include "schemasystem.h"
#include "entitysystem.h"

class CC4
{
public:
	DECLARE_SCHEMA_CLASS(CC4);

	SCHEMA_FIELD(GameTime_t, m_fArmedTime);
};