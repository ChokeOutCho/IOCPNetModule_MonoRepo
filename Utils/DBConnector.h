#pragma once

#include <mysql/mysql.h>
#include <windows.h>
#include <string>
#include <iostream>
#include <mutex>

#define DB_CONNECTOR_ERROR -1

enum class DBError
{
	NONE,
	NO_CONNECTION,
	QUERY_FAILED,
	INIT_FAILED,
	CONNECT_FAILED
};

struct DBConfig
{
	std::string host;
	std::string user;
	std::string password;
	std::string db;
	unsigned int port;
};


class DBConnector
{
public:
	DBConnector(const std::string& host,
		const std::string& user,
		const std::string& password,
		const std::string& db,
		unsigned int port = 3306)
	{
		m_config.host = host;
		m_config.user = user;
		m_config.password = password;
		m_config.db = db;
		m_config.port = port;

		{
			std::lock_guard<std::mutex> guard(m_sInitLock);
			if (!m_sLibraryInit)
			{
				mysql_library_init(0, nullptr, nullptr);
				m_sLibraryInit = true;
			}
		}

		Connect();
	}

	bool Connect()
	{
		if (m_conn) 
		{
			mysql_close(m_conn); // 타임아웃으로 끊겼을 경우에 정리못했던 핸들
			m_conn = nullptr;
		}

		m_conn = mysql_init(nullptr);
		if (!m_conn)
		{
			m_lastError = DBError::INIT_FAILED;
			m_lastErrorMsg = L"MySQL init failed";
			return false;
		}

		if (!mysql_real_connect(m_conn,
			m_config.host.c_str(),
			m_config.user.c_str(),
			m_config.password.c_str(),
			m_config.db.c_str(),
			m_config.port,
			nullptr, 0))
		{
			m_lastErrorNo = mysql_errno(m_conn);
			m_lastError = DBError::CONNECT_FAILED;
			m_lastErrorMsg = Utf8ToUtf16(mysql_error(m_conn));
			mysql_close(m_conn);
			m_conn = nullptr;
			return false;
		}
		else
		{
			m_lastError = DBError::NONE;
			m_lastErrorMsg.clear();
			return true;
		}
	}

	MYSQL_RES* Read(const std::string& sql)
	{
		m_lastQuery = sql;

		if (!m_conn)
		{
			m_lastError = DBError::NO_CONNECTION;
			m_lastErrorMsg = L"No connection";
			return nullptr;
		}
		if (mysql_query(m_conn, m_lastQuery.c_str()))
		{
			m_lastError = DBError::QUERY_FAILED;
			m_lastErrorMsg = Utf8ToUtf16(mysql_error(m_conn));
			return nullptr;
		}
		m_lastError = DBError::NONE;
		m_lastErrorMsg.clear();
		return mysql_store_result(m_conn);
	}

	long Write(const std::string& sql)
	{
		m_lastQuery = sql;

		if (!m_conn)
		{
			m_lastErrorNo = mysql_errno(m_conn);
			m_lastError = DBError::NO_CONNECTION;
			m_lastErrorMsg = L"No connection";
			return DB_CONNECTOR_ERROR;
		}
		if (mysql_query(m_conn, m_lastQuery.c_str()))
		{
			m_lastErrorNo = mysql_errno(m_conn);
			m_lastError = DBError::QUERY_FAILED;
			m_lastErrorMsg = Utf8ToUtf16(mysql_error(m_conn));
			return DB_CONNECTOR_ERROR;
		}
		m_lastErrorNo = mysql_errno(m_conn);
		m_lastError = DBError::NONE;
		m_lastErrorMsg.clear();
		return mysql_affected_rows(m_conn);
	}

	MYSQL_RES* Read(const std::wstring& sql)
	{
		std::string utf = Utf16ToUtf8(sql);
		return Read(std::move(utf));
	}

	long Write(const std::wstring& sql)
	{
		std::string utf = Utf16ToUtf8(sql);
		return Write(std::move(utf));
	}

	bool TryPingAndConnect()
	{
		if (mysql_ping(m_conn) == 0)
			return true; // 연결 살아있음

		return Connect(); // 끊겼으면 재연결 시도
	}

	DBError GetLastError() const { return m_lastError; }
	const std::wstring& GetLastErrorMsg() const { return m_lastErrorMsg; }
	long GetLastErrorNo() const { return m_lastErrorNo; }
	const std::string& GetLastQuery() const { return m_lastQuery; }
	~DBConnector()
	{
		if (m_conn) mysql_close(m_conn);
	}

private:
	MYSQL* m_conn;
	DBError m_lastError;
	std::wstring m_lastErrorMsg;
	std::string m_lastQuery;
	long m_lastErrorNo;
	inline static char m_sLibraryInit;
	inline static std::mutex m_sInitLock;
	DBConfig m_config;
	__inline static std::string Utf16ToUtf8(const std::wstring& wstr)
	{
		if (wstr.empty()) return std::string();

		int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0,
			wstr.c_str(),
			(int)wstr.size(),
			nullptr, 0,
			nullptr, nullptr);
		std::string result(sizeNeeded, 0);

		WideCharToMultiByte(CP_UTF8, 0,
			wstr.c_str(),
			(int)wstr.size(),
			&result[0], sizeNeeded,
			nullptr, nullptr);

		return std::move(result);
	}
	__inline static std::wstring Utf8ToUtf16(const std::string& str)
	{
		if (str.empty()) return std::wstring();

		int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0,
			str.c_str(),
			(int)str.size(),
			nullptr, 0);

		std::wstring result(sizeNeeded, 0);

		MultiByteToWideChar(CP_UTF8, 0,
			str.c_str(),
			(int)str.size(),
			&result[0], sizeNeeded);

		return std::move(result);
	}
};

class TLSDBConnector
{
public:
	TLSDBConnector(const std::string& host,
		const std::string& user,
		const std::string& password,
		const std::string& db,
		unsigned int port = 3306)
	{
		config.host = host;
		config.user = user;
		config.password = password;
		config.db = db;
		config.port = port;

		tlsIndex = TlsAlloc();
		if (tlsIndex == TLS_OUT_OF_INDEXES)
			throw std::runtime_error("TLS allocation failed");
	}

	~TLSDBConnector()
	{
		// TODO 어떻게 정리할지 모르겠음...
	}

	MYSQL_RES* Read(const std::string& query) { return GetInstance()->Read(query); }
	long Write(const std::string& query) { return GetInstance()->Write(query); }

	MYSQL_RES* Read(const std::wstring& query) { return GetInstance()->Read(query); }
	long Write(const std::wstring& query) { return GetInstance()->Write(query); }

	DBError GetLastError() { return GetInstance()->GetLastError(); }
	const std::wstring& GetLastErrorMsg() { return GetInstance()->GetLastErrorMsg(); }
	const std::string& GetLastQuery() { return GetInstance()->GetLastQuery(); }
	DBConnector* GetInstance()
	{
		DBConnector* inst = static_cast<DBConnector*>(TlsGetValue(tlsIndex));
		if (!inst)
		{
			inst = new DBConnector(config.host,
				config.user,
				config.password,
				config.db,
				config.port);
			TlsSetValue(tlsIndex, inst);
		}
		return inst;
	}

private:

	DBConfig config;
	DWORD tlsIndex;
};