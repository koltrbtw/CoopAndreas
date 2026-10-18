#pragma once

// a vtable of every vehicle class (CAutomobile's heirs with their own one too), for the hooks of a virtual method
// in all of them: the original is kept per vtable
class CVehicleVTables
{
public:
    static constexpr uintptr_t ALL[] = {
        0x871120,  // CAutomobile
        0x8721A0,  // CBoat
        0x871360,  // CBike
        0x871948,  // CPlane
        0x871680,  // CHeli
        0x871528,  // CBmx
        0x8717D8,  // CMonsterTruck
        0x871AE8,  // CQuadBike
        0x872370,  // CTrain
        0x871C28,  // CTrailer
    };

    // a virtual method hooked in every vehicle vtable
    template <typename THook>
    class Hook
    {
    public:
        void Install(unsigned int nSlotOffset, THook pHook)
        {
            for (size_t i = 0; i < std::size(ALL); i++)
            {
                uintptr_t slot = ALL[i] + nSlotOffset;
                m_aOriginals[i] = *reinterpret_cast<uintptr_t*>(slot);
                patch::SetPointer(slot, pHook);
            }
        }

        // the game's own method for this vehicle's class, as a thiscall
        template <typename TMethod>
        TMethod Original(CVehicle* pVehicle) const
        {
            uintptr_t vtable = *reinterpret_cast<uintptr_t*>(pVehicle);
            for (size_t i = 0; i < std::size(ALL); i++)
            {
                if (ALL[i] == vtable)
                {
                    return reinterpret_cast<TMethod>(m_aOriginals[i]);
                }
            }
            return nullptr;
        }

    private:
        uintptr_t m_aOriginals[std::size(ALL)]{};
    };
};
