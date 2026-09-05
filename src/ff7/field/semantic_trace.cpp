/****************************************************************************/
//    Copyright (C) 2026                                                     //
//                                                                          //
//    This file is part of FFNx                                             //
//                                                                          //
//    FFNx is free software: you can redistribute it and/or modify          //
//    it under the terms of the GNU General Public License as published by  //
//    the Free Software Foundation, either version 3 of the License         //
//    or (at your option) any later version.                                //
/****************************************************************************/

#include "semantic_trace.h"

#include "../../cfg.h"
#include "../../ff7.h"
#include "../../log.h"
#include "../../common.h"
#include "opcode.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

namespace ff7::field
{
    namespace
    {
        using OpcodeFunction = int (*)();

        std::array<uint32_t, OPCODE_COUNT> original_semantic_opcode_table {0};
        FILE* semantic_trace_file = nullptr;
        uint64_t semantic_trace_sequence = 0;
        bool semantic_trace_installed = false;

        template <std::size_t Opcode>
        int semantic_opcode_wrapper()
        {
            const auto entity = ff7_externals.current_entity_id == nullptr
                ? 0U
                : static_cast<unsigned int>(*ff7_externals.current_entity_id);
            const auto script_position = ff7_externals.field_curr_script_position == nullptr
                ? 0U
                : static_cast<unsigned int>(ff7_externals.field_curr_script_position[entity]);
            const auto* script = ff7_externals.field_script_ptr == nullptr
                ? nullptr
                : *ff7_externals.field_script_ptr;
            const auto script_opcode = script == nullptr ? 0U : script[script_position];
            char parameters[17] = {};
            if (script != nullptr)
            {
                for (unsigned int index = 0; index < 8; ++index)
                {
                    std::snprintf(&parameters[index * 2], 3, "%02X",
                                  script[script_position + 1 + index]);
                }
            }

            const auto field_id = common_externals.current_field_id == nullptr
                ? 0U
                : static_cast<unsigned int>(*common_externals.current_field_id);
            const auto sequence = semantic_trace_sequence++;
            std::fprintf(
                semantic_trace_file,
                "{\"type\":\"opcode\",\"sequence\":%llu,\"field_id\":%u,\"entity\":%u,\"ip_before\":%u,\"opcode\":%u,\"script_opcode\":%u,\"parameters\":\"%s\"}\n",
                static_cast<unsigned long long>(sequence), field_id, entity,
                script_position, static_cast<unsigned int>(Opcode), script_opcode,
                parameters);

            const auto result = ((OpcodeFunction)original_semantic_opcode_table[Opcode])();

            const auto ip_after = ff7_externals.field_curr_script_position == nullptr
                ? 0U
                : static_cast<unsigned int>(ff7_externals.field_curr_script_position[entity]);
            std::fprintf(
                semantic_trace_file,
                "{\"type\":\"opcode_result\",\"sequence\":%llu,\"field_id\":%u,\"entity\":%u,\"ip_after\":%u,\"result\":%d}\n",
                static_cast<unsigned long long>(sequence), field_id, entity, ip_after,
                result);

            if (ff7_externals.field_event_data_ptr != nullptr &&
                *ff7_externals.field_event_data_ptr != nullptr)
            {
                auto model_id = entity;
                if (ff7_externals.field_model_id_array != nullptr)
                {
                    model_id = ff7_externals.field_model_id_array[entity];
                }
                if (model_id < FF7_MAX_NUM_MODEL_ENTITIES)
                {
                    const auto& state = (*ff7_externals.field_event_data_ptr)[model_id];
                    std::fprintf(
                        semantic_trace_file,
                        "{\"type\":\"entity_state\",\"sequence\":%llu,\"field_id\":%u,\"entity\":%u,\"model\":%u,\"x\":%d,\"y\":%d,\"z\":%d,\"direction\":%d,\"triangle\":%d,\"character\":%d,\"animation\":%d}\n",
                        static_cast<unsigned long long>(sequence), field_id, entity, model_id,
                        state.model_pos.x, state.model_pos.y, state.model_pos.z,
                        state.field_direction_or_collision, state.field_triangle_id,
                        state.character_id, state.animation_id);
                }
            }
            std::fflush(semantic_trace_file);
            return result;
        }

        template <std::size_t... Opcode>
        std::array<uint32_t, sizeof...(Opcode)> make_wrapper_table(
            std::index_sequence<Opcode...>)
        {
            return {
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&semantic_opcode_wrapper<Opcode>))...
            };
        }

        const auto semantic_wrapper_table = make_wrapper_table(
            std::make_index_sequence<OPCODE_COUNT>());
    }

    void semantic_trace_install()
    {
        if (!trace_semantic || semantic_trace_installed)
        {
            return;
        }

        semantic_trace_file = std::fopen(trace_semantic_path.c_str(), "wb");
        if (semantic_trace_file == nullptr)
        {
            ffnx_warning("Unable to open semantic trace file: %s\n", trace_semantic_path.c_str());
            return;
        }

        std::memcpy(original_semantic_opcode_table.data(),
                    common_externals.execute_opcode_table,
                    sizeof(original_semantic_opcode_table));
        std::fprintf(semantic_trace_file,
                     "{\"type\":\"trace_start\",\"format\":1,\"opcode_count\":%u}\n",
                     static_cast<unsigned int>(OPCODE_COUNT));
        std::memcpy(common_externals.execute_opcode_table,
                    semantic_wrapper_table.data(),
                    sizeof(semantic_wrapper_table));
        std::fflush(semantic_trace_file);
        semantic_trace_installed = true;
        ffnx_info("Semantic field trace enabled: %s\n", trace_semantic_path.c_str());
    }
}
