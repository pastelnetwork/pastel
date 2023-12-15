// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <rpc/rpc_parser.h>
#include <rpc/server.h>
#include <mnode/rpc/generate-report.h>
#include <mnode/rpc/rpt-fees-burn.h>

using namespace std;

UniValue generate_report(const UniValue& vParams, const bool fHelp)
{
    RPC_CMD_PARSER(GENRPT, vParams, fees__and__burn);
	if (fHelp || !GENRPT.IsCmdSupported())
		throw runtime_error(
R"(generate-report "report-name"...
Generate various reports

Available reports:
  fees-and-burn ... - Pastel Network blockchain fees and burn analysis.

Examples:
)"
+ HelpExampleCli("generate-report", "fees-and-burn")
+ HelpExampleRpc("generate-report", "fees-and-burn")
);
	
    UniValue rptObj(UniValue::VOBJ);
	string strReportName, strError;
    switch (GENRPT.cmd())
    {
        case RPC_CMD_GENRPT::fees__and__burn:
			rptObj = generate_report_fees_and_burn(vParams);
			break;

        default:
            break;
    }
    return rptObj;
}
