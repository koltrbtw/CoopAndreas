#pragma once

// the animations of a network ped: the syncer sends what plays on its ped, the clone plays the same set. The move
// state alone gives only walking and standing, so gestures, chats and fights were lost and the clone slid on an idle
class CPedAnimSync
{
public:
    // returns the number written to pAnims
    static int Collect(CPed* pPed, Packets::Peds::SPedAnim* pAnims, int nMaxAnims);
    static void Apply(CPed* pPed, const Packets::Peds::SPedAnim* pAnims, int nCount);
    // the ped became ours: fade out what the packets kept on it, its own tasks play their anims now
    static void Release(CPed* pPed);
};
