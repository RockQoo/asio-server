#include "pch.h"
#include "Processor/ProcessorIds.h"

WorldProcessorIds& Ids()
{
    static WorldProcessorIds ids;
    return ids;
}
