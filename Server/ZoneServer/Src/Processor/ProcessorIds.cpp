#include "pch.h"
#include "Processor/ProcessorIds.h"

ZoneProcessorIds& Ids()
{
    static ZoneProcessorIds ids;
    return ids;
}
