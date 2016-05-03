/***************************************************************************
 *   Copyright (C) 2014 by Tobias Volk                                     *
 *   mail@tobiasvolk.de                                                    *
 *                                                                         *
 *   This program is free software: you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation, either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/


#ifndef F_AUTHMGT_C
#define F_AUTHMGT_C

#include "logging.h"

#include "auth.h"
#include "idsp.h"
#include "p2p.h"


// Return number of auth slots.
int authmgtSlotCount(struct s_authmgt *mgt) {
	return idspSize(&mgt->idsp);
}


// Return number of used auth slots.
int authmgtUsedSlotCount(struct s_authmgt *mgt) {
	return idspUsedCount(&mgt->idsp);	
}


// Create new auth session. Returns ID of session if successful.
int authmgtNew(struct s_authmgt *mgt, const struct s_peeraddr *peeraddr) {
	int authstateid = idspNew(&mgt->idsp);
	int tnow = utilGetClock();
	
	if(authstateid < 0) {
		return -1;
	}

	mgt->lastsend[authstateid] = (mgt->fastauth) ? (tnow - authmgt_RESEND_TIMEOUT - 3) : tnow;
	mgt->lastrecv[authstateid] = tnow;
	mgt->peeraddr[authstateid] = *peeraddr;
    
    CREATE_HUMAN_IP(peeraddr);
    debugf("Starting new auth session for %s, ID: %d", humanIp, authstateid);
	
    return authstateid;
}


// Delete auth session.
void authmgtDelete(struct s_authmgt *mgt, const int authstateid) {
	if(mgt->current_authed_id == authstateid) mgt->current_authed_id = -1;
	if(mgt->current_completed_id == authstateid) mgt->current_completed_id = -1;
	authReset(&mgt->authstate[authstateid]);
	idspDelete(&mgt->idsp, authstateid);
}


// Start new auth session. Returns 1 on success.
int authmgtStart(struct s_authmgt *mgt, const struct s_peeraddr *peeraddr) {
	int authstateid = authmgtNew(mgt, peeraddr);
	if(authstateid < 0) {
		return 0;
	}
	
	authStart(&mgt->authstate[authstateid]);
	return 1;
}


// Check if auth manager has an authed peer.
int authmgtHasAuthedPeer(struct s_authmgt *mgt) {
	return (!(mgt->current_authed_id < 0));
}


// Get the NodeID of the current authed peer.
int authmgtGetAuthedPeerNodeID(struct s_authmgt *mgt, struct s_nodeid *nodeid) {
	if(authmgtHasAuthedPeer(mgt)) {
		return authGetRemoteNodeID(&mgt->authstate[mgt->current_authed_id], nodeid);
	}
	else {
		return 0;
	}
}


// Accept the current authed peer.
void authmgtAcceptAuthedPeer(struct s_authmgt *mgt, const int local_peerid, const int64_t seq, const int64_t flags) {
	if(authmgtHasAuthedPeer(mgt)) {
		authSetLocalData(&mgt->authstate[mgt->current_authed_id], local_peerid, seq, flags);
		mgt->current_authed_id = -1;
	}
}


// Reject the current authed peer.
void authmgtRejectAuthedPeer(struct s_authmgt *mgt) {
	if(authmgtHasAuthedPeer(mgt)) authmgtDelete(mgt, mgt->current_authed_id);
}


// Check if auth manager has a completed peer.
int authmgtHasCompletedPeer(struct s_authmgt *mgt) {
	return (!(mgt->current_completed_id < 0));
}


// Get the local PeerID of the current completed peer.
int authmgtGetCompletedPeerLocalID(struct s_authmgt *mgt) {
	if(authmgtHasCompletedPeer(mgt)) {
		return mgt->authstate[mgt->current_completed_id].local_peerid;
	}
	else {
		return 0;
	}
}


// Get the NodeID of the current completed peer.
int authmgtGetCompletedPeerNodeID(struct s_authmgt *mgt, struct s_nodeid *nodeid) {
	if(authmgtHasCompletedPeer(mgt)) {
		return authGetRemoteNodeID(&mgt->authstate[mgt->current_completed_id], nodeid);
	}
	else {
		return 0;
	}
}


// Get the remote PeerID and PeerAddr of the current completed peer.
int authmgtGetCompletedPeerAddress(struct s_authmgt *mgt, int *remote_peerid, struct s_peeraddr *remote_peeraddr) {
	if(!authmgtHasCompletedPeer(mgt)) {
		return 0;
	}

	if(!authGetRemotePeerID(&mgt->authstate[mgt->current_completed_id], remote_peerid)) return 0;
	*remote_peeraddr = mgt->peeraddr[mgt->current_completed_id];
	
	return 1;
}


// Get the shared session keys of the current completed peer.
int authmgtGetCompletedPeerSessionKeys(struct s_authmgt *mgt, struct s_crypto *ctx) {
	if(authmgtHasCompletedPeer(mgt)) {
		return authGetSessionKeys(&mgt->authstate[mgt->current_completed_id], ctx);
	}
	else {
		return 0;
	}
}


// Get the connection parameters of the current completed peer.
int authmgtGetCompletedPeerConnectionParams(struct s_authmgt *mgt, int64_t *remoteseq, int64_t *remoteflags) {
	if(authmgtHasCompletedPeer(mgt)) {
		return authGetConnectionParams(&mgt->authstate[mgt->current_completed_id], remoteseq, remoteflags);
	}
	else {
		return 0;
	}
}

// Finish the current completed peer.
void authmgtFinishCompletedPeer(struct s_authmgt *mgt) {
	mgt->current_completed_id = -1;
}


// Get next auth manager message.
int authmgtGetNextMsg(struct s_authmgt *mgt, struct s_msg *out_msg, struct s_peeraddr *target) {
	int used = idspUsedCount(&mgt->idsp);
	int tnow = utilGetClock();
	int authstateid;
	int i;
    
	for(i=0; i<used; i++) {
		authstateid = idspNext(&mgt->idsp);
		if((tnow - mgt->lastrecv[authstateid]) >= AUTHMGT_RECV_TIMEOUT) { // check if auth session has expired
            authmgtDelete(mgt, authstateid);
            continue;
        }
        
        if((tnow - mgt->lastsend[authstateid]) <= AUTHMGT_RESEND_TIMEOUT) { // only send one auth message per specified time interval and session
            continue;
        }
        
        if(authGetNextMsg(&mgt->authstate[authstateid], out_msg)) {
            mgt->lastsend[authstateid] = tnow;
            *target = mgt->peeraddr[authstateid];
            
            CREATE_HUMAN_IP(target);
            debugf("[%d] New AUTH packet for %s created, size: %d", authstateid, humanIp, out_msg->len);
            
            return 1;
        }
    }
    
	return 0;
}


// Find auth session with specified PeerAddr.
int authmgtFindAddr(struct s_authmgt *mgt, const struct s_peeraddr *addr) {
	int count = idspUsedCount(&mgt->idsp);
	int i;
	int j;
	for(i=0; i<count; i++) {
		j = idspNext(&mgt->idsp);
		if(memcmp(mgt->peeraddr[j].addr, addr->addr, peeraddr_SIZE) == 0) return j;
	}
	return -1;
}


// Find unused auth session.
int authmgtFindUnused(struct s_authmgt *mgt) {
	int count = idspUsedCount(&mgt->idsp);
	int i;
	int j;
	struct s_auth_state *authstate;
	for(i=0; i<count; i++) {
		j = idspNext(&mgt->idsp);
		authstate = &mgt->authstate[j];
		if((!authIsPreauth(authstate)) || (authIsPeerCompleted(authstate))) return j;
	}
	return -1;
}


// Decode auth message. Returns 1 if message is accepted.
int authmgtDecodeMsg(struct s_authmgt *mgt, const unsigned char *msg, const int msg_len, const struct s_peeraddr *peeraddr) {
	int authid;
	int authstateid;
	int tnow = utilGetClock();
	int newsession;
	int dupid;
    
    CREATE_HUMAN_IP(peeraddr);
    
    debugf("[%s] AUTH message received", humanIp);
    
	if(msg_len <= 4) {
        debugf("[%s] Wrong AUTH message size: %d", humanIp, msg_len);
        return 0;
    }
    
    authid = utilReadInt32(msg);
    if(authid > 0) {
        // message belongs to existing auth session
        authstateid = (authid - 1);
        
        debugf("Found active auth session: %d", authstateid);
        if(authstateid >= idspSize(&mgt->idsp)) {
            debugf("[%s] wrong auth state ID", humanIp);
            return 0;
        }
        
        if(!authDecodeMsg(&mgt->authstate[authstateid], msg, msg_len)) {
            debugf("[%s] failed to decode AUTH message", humanIp);
            return 0;
        }
        
        mgt->lastrecv[authstateid] = tnow;
        mgt->peeraddr[authstateid] = *peeraddr;
        if(mgt->fastauth) {
            mgt->lastsend[authstateid] = (tnow - authmgt_RESEND_TIMEOUT - 3);
        }
        
        if((authIsAuthed(&mgt->authstate[authstateid])) && (!authIsCompleted(&mgt->authstate[authstateid]))) mgt->current_authed_id = authstateid;
        
        if((authIsCompleted(&mgt->authstate[authstateid])) && (!authIsPeerCompleted(&mgt->authstate[authstateid]))) {
            msgf("Host %s authorized", humanIp);
            mgt->current_completed_id = authstateid;
        }
        
        return 1;
    } else if(authid == 0) {
        // message requests new auth session
        dupid = authmgtFindAddr(mgt, peeraddr);
        
        // we already have this session
        if(dupid >= 0) {
            // auth session with same PeerAddr found.
            if(authIsPreauth(&mgt->authstate[dupid])) {
                return 0;
            }
        
            authmgtDelete(mgt, dupid);
        }
        
        authstateid = authmgtNew(mgt, peeraddr);
        if(authstateid < 0) {
            // all auth slots are full, search for unused sessions that can be replaced
            dupid = authmgtFindUnused(mgt);
            if(!(dupid < 0)) {
                authmgtDelete(mgt, dupid);
                authstateid = authmgtNew(mgt, peeraddr);
            }
        }
        
        if(!(authstateid < 0)) {
            if(authDecodeMsg(&mgt->authstate[authstateid], msg, msg_len)) {
                mgt->lastrecv[authstateid] = tnow;
                mgt->peeraddr[authstateid] = *peeraddr;
                if(mgt->fastauth) {
                    mgt->lastsend[authstateid] = (tnow - authmgt_RESEND_TIMEOUT - 3);
                }
                return 1;
            }
            else {
                authmgtDelete(mgt, authstateid);
            }
        }
        
    }
    
    return 0;
}


// Enable/disable fastauth (ignore send timeout after auth status change)
void authmgtSetFastauth(struct s_authmgt *mgt, const int enable) {
    mgt->fastauth = (enable);
}


// Reset auth manager object.
void authmgtReset(struct s_authmgt *mgt) {
	int i;
	int count = idspSize(&mgt->idsp);
	for(i=0; i<count; i++) {
		authReset(&mgt->authstate[i]);
	}
    
	idspReset(&mgt->idsp);
	mgt->fastauth = 0;
	mgt->current_authed_id = -1;
	mgt->current_completed_id = -1;
    
    debug("auth manager RESET completed");
}


// Create auth manager object.
int authmgtCreate(struct s_authmgt *mgt, struct s_netid *netid, const int auth_slots, struct s_nodekey *local_nodekey, struct s_dh_state *dhstate) {
    debug("Initializing AuthMgt");
    
	int ac;
	struct s_auth_state *authstate_mem;
	struct s_peeraddr *peeraddr_mem;
	int *lastsend_mem;
	int *lastrecv_mem;
    
	if(auth_slots <= 0) {
        debug("No auth slots available");
        return 0;
    }
    
    lastsend_mem = malloc(sizeof(int) * auth_slots);
    if(lastsend_mem == NULL) {
        debug("failed to allocate memory for send_mem / auth_slots");
        return 0;
    }
    
    lastrecv_mem = malloc(sizeof(int) * auth_slots);
    if(lastrecv_mem == NULL) {
        debug("failed to allocate memory for recv_mem / auth_slots");
        return 0;
    }
    
    authstate_mem = malloc(sizeof(struct s_auth_state) * auth_slots);
    
    if(authstate_mem == NULL) {
        free(authstate_mem);
        debug("failed to allocate memory for authstate");
        return 0;
    }
    
    peeraddr_mem = malloc(sizeof(struct s_peeraddr) * auth_slots);
    if(peeraddr_mem == NULL) {
        free(authstate_mem); free(peeraddr_mem);
        debug("failed to allocate memory for peeraddr_mem");
        return 0;
    }
    
    ac = 0;
    while(ac < auth_slots) {
        if(!authCreate(&authstate_mem[ac], netid, local_nodekey, dhstate, (ac + 1))) break;
        ac++;
    }
    
    if(!(ac < auth_slots)) {
        if(idspCreate(&mgt->idsp, auth_slots)) {
            mgt->lastsend = lastsend_mem;
            mgt->lastrecv = lastrecv_mem;
            mgt->authstate = authstate_mem;
            mgt->peeraddr = peeraddr_mem;
            authmgtReset(mgt);
            return 1;
        }
    }
    while(ac > 0) {
        ac--;
        authDestroy(&authstate_mem[ac]);
    }
    

    return 0;
}


// Destroy auth manager object.
void authmgtDestroy(struct s_authmgt *mgt) {
	int i;
	int count = idspSize(&mgt->idsp);
	idspDestroy(&mgt->idsp);
	for(i=0; i<count; i++) authDestroy(&mgt->authstate[i]);
	free(mgt->peeraddr);
	free(mgt->authstate);
	free(mgt->lastrecv);
	free(mgt->lastsend);
}


#endif // F_AUTHMGT_C
