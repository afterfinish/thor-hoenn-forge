// Copyright Hoenn Forge — pk3DS GARCReference_AO paths
package org.citra.citra_emu.hoennforge.randomizer

object OrasPaths {
    // CRO
    const val POKE3_SELECT = "DllPoke3Select.cro"
    const val FIELD = "DllField.cro"

    // GARC file number → a/A/B/C
    const val ENCDATA = "a/0/1/3" // 013
    const val TRDATA = "a/0/3/6" // 036
    const val TRPOKE = "a/0/3/8" // 038
    const val MOVE = "a/1/8/9" // 189
    const val EGGMOVE = "a/1/9/0" // 190
    const val LEVELUP = "a/1/9/1" // 191
    const val EVOLUTION = "a/1/9/2" // 192
    const val PERSONAL = "a/1/9/5" // 195
    const val ITEM = "a/1/9/7" // 197

    // Starter editor (pk3DS StarterEditor6)
    const val FIELD_GIFT_OFFSET = 0xF906C
    const val FIELD_GIFT_SIZE = 0x24
    val FIELD_STARTER_ENTRIES = intArrayOf(
        0, 1, 2, // Gen 3
        28, 29, 30, // Gen 2
        31, 32, 33, // Gen 4
        34, 35, 36, // Gen 5
    )

    // Static encounters in DllField
    const val FIELD_STATIC_OFFSET = 0xF1B20
    const val FIELD_STATIC_SIZE = 0xC
    const val FIELD_STATIC_COUNT = 0x3B

    const val PERSONAL_SIZE = 0x50
    const val SLOT_COUNT = 61 // RSWE All_Species length
    const val ENCOUNTER_TABLE = 0xF6
}
