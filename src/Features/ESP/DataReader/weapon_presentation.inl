    {
        struct PresentationCache {
            uintptr_t pawn=0, weapon=0, vdata=0;
            uint32_t pawnHandle=0;
            uint16_t id=0;
            uint64_t lastUs=0, metadataUs=0;
            int clip=-1, reserve=-1, capacity=-1;
            uint8_t clips=2;
            bool reloading=false;
        };
        static std::array<PresentationCache,64> cache{};
        static uint64_t cacheScene=0, attemptUs=0;
        const uint64_t scene=s_sceneResetSerial.load(std::memory_order_relaxed);
        if (scene!=cacheScene) { cache={}; cacheScene=scene; attemptUs=0; }
        const bool due=settingsSnapshot.espWeaponPresentation &&
            (!attemptUs || nowUs<attemptUs || nowUs-attemptUs>=50000u);
        if (due) {
            attemptUs=nowUs;
            esp::data::CheckedScatterBatch<448> batch;
            std::array<std::array<size_t,6>,64> requests;
            for (auto& row:requests) row.fill(batch.invalid);
            std::array<PresentationCache,64> samples{};
            std::array<uint8_t,64> reload{};
            std::array<uint16_t,64> identities{};
            for (int i=0;i<64;++i) {
                const auto& player=s_players[i];
                if (!player.valid || player.health<=0 || player.pawn!=pawns[i]) { cache[i]={}; continue; }
                uintptr_t weapon=activeWeapons[i];
                if (player.weaponIconId!=player.weaponId) {
                    weapon=0;
                    for (int slot=0;slot<getInventorySlotCount(i);++slot)
                        if (inventoryWeaponIds[i][slot]==player.weaponIconId) {
                            weapon=inventoryWeapons[i][slot]; break;
                        }
                }
                if (!isLikelyGamePointer(weapon) || !player.weaponIconId ||
                    esp::weapons::WeaponMaxClipFromItemId(player.weaponIconId)<=0) { cache[i]={}; continue; }
                auto& c=cache[i];
                if (c.pawn!=player.pawn || c.pawnHandle!=player.pawnHandle ||
                    c.weapon!=weapon || c.id!=player.weaponIconId) {
                    c={}; c.pawn=player.pawn; c.pawnHandle=player.pawnHandle;
                    c.weapon=weapon; c.id=player.weaponIconId;
                }
                auto& sample=samples[i];
                if (ofs.C_BasePlayerWeapon_m_iClip1>0)
                    requests[i][0]=batch.Add(weapon+ofs.C_BasePlayerWeapon_m_iClip1,&sample.clip,sizeof(int));
                if (ofs.C_BasePlayerWeapon_m_pReserveAmmo>0)
                    requests[i][1]=batch.Add(weapon+ofs.C_BasePlayerWeapon_m_pReserveAmmo,&sample.reserve,sizeof(int));
                if (isLikelyGamePointer(activeWeapons[i]) && ofs.C_CSWeaponBase_m_bInReload>0)
                    requests[i][2]=batch.Add(activeWeapons[i]+ofs.C_CSWeaponBase_m_bInReload,&reload[i],sizeof(uint8_t));
                if (ofs.C_BaseEntity_m_nSubclassID>0 &&
                    (!c.metadataUs || nowUs<c.metadataUs || nowUs-c.metadataUs>1000000u))
                    requests[i][3]=batch.Add(weapon+ofs.C_BaseEntity_m_nSubclassID+8,&sample.vdata,sizeof(uintptr_t));
                if (ofs.C_EconEntity_m_AttributeManager>0 && ofs.C_AttributeContainer_m_Item>0 &&
                    ofs.C_EconItemView_m_iItemDefinitionIndex>0)
                    requests[i][4]=batch.Add(weapon+ofs.C_EconEntity_m_AttributeManager+
                        ofs.C_AttributeContainer_m_Item+ofs.C_EconItemView_m_iItemDefinitionIndex,
                        &identities[i],sizeof(uint16_t));
            }
            batch.Execute(mem,handle);
            esp::data::CheckedScatterBatch<128> metadata;
            std::array<std::array<size_t,2>,64> metaRequests;
            for(auto& row:metaRequests) row.fill(metadata.invalid);
            for(int i=0;i<64;++i) {
                auto& c=cache[i]; auto& sample=samples[i];
                if (!c.weapon) continue;
                if (!batch.Complete(requests[i][4]) || identities[i]!=c.id) { c.lastUs=0; continue; }
                if (batch.Complete(requests[i][0]) && sample.clip>=0 && sample.clip<=1000) {
                    c.clip=sample.clip; c.lastUs=nowUs;
                    c.reserve=batch.Complete(requests[i][1]) && sample.reserve>=0 && sample.reserve<=10000 ? sample.reserve : -1;
                    c.reloading=batch.Complete(requests[i][2]) && reload[i]==1;
                } else c.lastUs=0;
                if (batch.Complete(requests[i][3]) && isLikelyGamePointer(sample.vdata)) {
                    c.vdata=sample.vdata; c.metadataUs=nowUs; c.clips=2; c.capacity=-1;
                    if (ofs.CBasePlayerWeaponVData_m_iMaxClip1>0)
                        metaRequests[i][0]=metadata.Add(c.vdata+ofs.CBasePlayerWeaponVData_m_iMaxClip1,&sample.capacity,sizeof(int));
                    if (ofs.CBasePlayerWeaponVData_m_bReserveAmmoAsClips>0)
                        metaRequests[i][1]=metadata.Add(c.vdata+ofs.CBasePlayerWeaponVData_m_bReserveAmmoAsClips,&sample.clips,sizeof(uint8_t));
                }
            }
            metadata.Execute(mem,handle);
            for(int i=0;i<64;++i) {
                if(metadata.Complete(metaRequests[i][0]) && samples[i].capacity>0 && samples[i].capacity<=1000)
                    cache[i].capacity=samples[i].capacity;
                if(metadata.Complete(metaRequests[i][1]) && samples[i].clips<=1) cache[i].clips=samples[i].clips;
            }
        }
        for(int i=0;i<64;++i) {
            auto& player=s_players[i]; const auto& c=cache[i];
            const bool fresh=settingsSnapshot.espWeaponPresentation && player.valid && player.health>0 &&
                c.pawn==player.pawn && c.pawnHandle==player.pawnHandle && c.id==player.weaponIconId &&
                c.lastUs && nowUs>=c.lastUs && nowUs-c.lastUs<=250000u;
            player.displayAmmoClip=fresh ? c.clip : -1;
            player.displayAmmoReserve=fresh ? c.reserve : -1;
            player.displayAmmoCapacity=fresh ? c.capacity : -1;
            player.displayReserveUnitsKnown=fresh && c.clips<=1;
            player.displayReserveAsClips=fresh && c.clips==1;
            player.isReloading=fresh && c.reloading;
            player.weaponPresentationUpdatedUs=fresh ? c.lastUs : 0;
        }
    }
