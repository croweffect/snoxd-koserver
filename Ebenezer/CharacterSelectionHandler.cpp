﻿#include "StdAfx.h"

void CUser::SelNationToAgent(Packet & pkt)
{
	Packet result(WIZ_SEL_NATION);
	uint8 nation = pkt.read<uint8>();
	if (nation != KARUS && nation != ELMORAD)
	{
		result << uint8(0);
		Send(&result);
		return;
	}

	result << nation; 
	g_pMain->AddDatabaseRequest(result, this);
}

void CUser::AllCharInfoToAgent()
{
	Packet result(WIZ_ALLCHAR_INFO_REQ);
	g_pMain->AddDatabaseRequest(result, this);
}

void CUser::ChangeHair(Packet & pkt)
{
	std::string strUserID;
	uint32 nHair;
	uint8 bOpcode, bFace;

	// The opcode:
	// 0 seems to be an in-game implementation for converting from old -> new hair data
	// 2 seems to be used with the hair change item(?).

	// Another note: there's 4 bytes at the end of the packet data that I can't account for (maybe a[nother] checksum?)

	pkt.SByte();
	pkt >> bOpcode >> strUserID >> bFace >> nHair;
	
	Packet result(WIZ_CHANGE_HAIR);
	result.SByte();
	result << bOpcode << strUserID << bFace << nHair;
	g_pMain->AddDatabaseRequest(result, this);
}

void CUser::NewCharToAgent(Packet & pkt)
{
	Packet result(WIZ_NEW_CHAR);
	uint32 nHair;
	uint16 sClass;
	uint8 bCharIndex, bRace, bFace, str, sta, dex, intel, cha, errorCode = 0;
	std::string strUserID;

	pkt	>> bCharIndex >> strUserID >> bRace >> sClass >> bFace >> nHair
		>> str >> sta >> dex >> intel >> cha;

	_CLASS_COEFFICIENT* p_TableCoefficient = g_pMain->m_CoefficientArray.GetData(sClass);

	if (!IsValidName(strUserID.c_str()))
		errorCode = NEWCHAR_INVALID_NAME;
	else if (bCharIndex > 2)
		errorCode = NEWCHAR_NO_MORE;
	else if (p_TableCoefficient == NULL
			|| (str + sta + dex + intel + cha) > 300) 
		errorCode = NEWCHAR_INVALID_DETAILS;
	else if (str < 50 || sta < 50 || dex < 50 || intel < 50 || cha < 50) 
		errorCode = NEWCHAR_STAT_TOO_LOW;

	if (errorCode != 0)
	{
		result << errorCode;
		Send(&result);
		return;
	}
	
	result	<< bCharIndex 
			<< strUserID << bRace << sClass << bFace << nHair
			<< str << sta << dex << intel << cha;
	g_pMain->AddDatabaseRequest(result, this);
}

void CUser::DelCharToAgent(Packet & pkt)
{
	Packet result(WIZ_DEL_CHAR);
	std::string strUserID, strSocNo;
	uint8 bCharIndex;
	pkt >> bCharIndex >> strUserID >> strSocNo; 

	if (bCharIndex > 2
		|| strUserID.empty() || strUserID.size() > MAX_ID_SIZE
		|| strSocNo.empty() || strSocNo.size() > 15
		|| isClanLeader())
	{
		result << uint8(0) << uint8(-1);
		Send(&result);
		return;
	}

	// Send packet to Aujard
	result	<< bCharIndex << strUserID << strSocNo;
	g_pMain->AddDatabaseRequest(result, this);
}

void CUser::SelCharToAgent(Packet & pkt)
{
	Packet result(WIZ_SEL_CHAR);
	std::string strUserID, strAccountID;
	uint8 bInit;

	pkt >> strAccountID >> strUserID >> bInit;
	if (strAccountID.empty() || strAccountID.size() > MAX_ID_SIZE
		|| strUserID.empty() || strUserID.size() > MAX_ID_SIZE
		|| strAccountID != m_strAccountID)
	{
		Disconnect();
		return;
	}

	// Disconnect any currently logged in sessions.
	CUser *pUser = g_pMain->GetUserPtr(strUserID.c_str(), TYPE_CHARACTER);
	if (pUser && (pUser->GetSocketID() != GetSocketID()))
	{
		pUser->Disconnect();

		// And reject the login attempt (otherwise we'll probably desync char data)
		result << uint8(0);
		Send(&result);
		return;
	}

	result << strUserID << bInit;
	g_pMain->AddDatabaseRequest(result, this);
}

void CUser::SelectCharacter(Packet & pkt)
{
	Packet result(WIZ_SEL_CHAR);
	uint8 bResult, bInit;

	if (isBanned())
	{
		Disconnect();
		return;
	}

	pkt >> bResult >> bInit;
	result << bResult;

	if (bResult == 0 || !GetZoneID()) 
		goto fail_return;

	m_pMap = g_pMain->GetZoneByID(GetZoneID());
	if (GetMap() == NULL)
		goto fail_return;

	if (g_pMain->m_nServerNo != GetMap()->m_nServerNo)
	{
		_ZONE_SERVERINFO *pInfo = g_pMain->m_ServerArray.GetData(GetMap()->m_nServerNo);
		if (pInfo == NULL) 
			goto fail_return;

		SendServerChange(pInfo->strServerIP, bInit);
		return;
	}

	if (g_pMain->m_byBattleOpen == NO_BATTLE && getFame() == COMMAND_CAPTAIN)
		m_bFame = CHIEF;

	if ((GetZoneID() != GetNation() && GetZoneID() < 3 && !g_pMain->m_byBattleOpen)
		|| (GetZoneID() == ZONE_BATTLE && (g_pMain->m_byBattleOpen != NATION_BATTLE))
		|| (GetZoneID() == ZONE_SNOW_BATTLE && (g_pMain->m_byBattleOpen != SNOW_BATTLE))
		|| (GetZoneID() == ZONE_FRONTIER && g_pMain->m_byBattleOpen))
	{
		NativeZoneReturn();
		Disconnect();
		return;
	}

	SetLogInInfoToDB(bInit);

	result << GetZoneID() << GetSPosX() << GetSPosZ() << GetSPosY() << g_pMain->m_byOldVictory;
	m_bSelectedCharacter = true;
	Send(&result);

	SetSlotItemValue();
	SetUserAbility(false);

	if (GetLevel() > MAX_LEVEL) 
	{
		Disconnect();
		return;
	}

	m_iMaxExp = g_pMain->GetExpByLevel(GetLevel());
	SetRegion(GetNewRegionX(), GetNewRegionZ());

	if (GetClanID() == -1)
	{
		SetClanID(0);
		m_bFame = 0;
		return;
	}
	else if (GetClanID() != 0)
	{
		CKnights* pKnights = g_pMain->GetClanPtr( GetClanID() );
		if (pKnights != NULL)
		{
			g_pMain->m_KnightsManager.SetKnightsUser(GetClanID(), GetName());
		}
		else if (GetZoneID() > 2)
		{
			result.Initialize(WIZ_KNIGHTS_PROCESS);
			result << uint8(KNIGHTS_LIST_REQ) << GetClanID();
			g_pMain->AddDatabaseRequest(result, this);
		}
	}
	return;

fail_return:
	Send(&result);
}

void CUser::SendServerChange(char *ip, uint8 bInit)
{
	Packet result(WIZ_SERVER_CHANGE);
	result << ip << uint16(_LISTEN_PORT) << bInit << GetZoneID() << g_pMain->m_byOldVictory;
	Send(&result);
}

// happens on character selection
void CUser::SetLogInInfoToDB(BYTE bInit)
{
	_ZONE_SERVERINFO *pInfo = g_pMain->m_ServerArray.GetData(g_pMain->m_nServerNo);
	if (pInfo == NULL) 
	{
		Disconnect();
		return;
	}

	Packet result(WIZ_LOGIN_INFO);
	result	<< GetName() 
			<< pInfo->strServerIP << uint16(_LISTEN_PORT) << GetRemoteIP() 
			<< bInit;
	g_pMain->AddDatabaseRequest(result, this);
}

void CUser::RecvLoginInfo(Packet & pkt)
{
	int8 bResult = pkt.read<uint8>();
	if (bResult < 0)
		Disconnect();
}

// This packet actually contains the char name after the opcode
void CUser::GameStart(Packet & pkt)
{
	if (isInGame())
		return;

	uint8 opcode = pkt.read<uint8>();

	if (opcode == 1)
	{
		SendMyInfo();
		g_pMain->UserInOutForMe(this);
		g_pMain->MerchantUserInOutForMe(this);
		g_pMain->NpcInOutForMe(this);
		SendNotice();
		SendTimeStatus();

		// SendHackToolList();

		Packet result(WIZ_GAMESTART);
		Send(&result);
	}
	else if (opcode == 2)
	{
		m_state = GAME_STATE_INGAME;
		UserInOut(INOUT_RESPAWN);

		if (!m_bCity && m_sHp <= 0)
			m_bCity = -1;

		if (m_bCity > 0)
		{
			int level = GetLevel();
			if (m_bCity <= 100)
				level--;

			// make sure we don't exceed bounds
			if (level > MAX_LEVEL)
				level = MAX_LEVEL;
			else if (level < 1)
				level = 1;

			m_iLostExp = (g_pMain->GetExpByLevel(level) * (m_bCity % 10) / 100);
			if (((m_bCity % 10) / 100) == 1)
				m_iLostExp /= 2;
		}
		else
		{
			m_iLostExp = 0;
		}

		BlinkStart();
		SetUserAbility();
		// rental
		// ItemMallMagicRecast();


		// If we've relogged while dead, we need to make sure the client 
		// is still given the option to revive.
		if (isDead())
			SendDeathAnimation();
	}
}