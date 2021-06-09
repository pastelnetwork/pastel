#!/usr/bin/env python3

import subprocess
import sys
import time
import smtplib
import shutil
import multiprocessing
from email.mime.multipart import MIMEMultipart
from email.mime.application import MIMEApplication
import os
import argparse


DEFAULT_EMAIL="pasteltest51@gmail.com"
DEFAULT_SMTP_SERVER="smtp.gmail.com"
DEFAULT_SMTP_PORT=587

def main(pwd, from_mail , to_mail, pvs_mail, pvs_lic_nr, nJobCount):
    timestr = time.strftime("%Y%m%d_%H%M%S")
    dirname = f"static_code_analysis_{timestr}"
    passed = False

    print("Pastel Static Code Analyzer (PVS Studio)")
    try:
        # get pastel root directory
        scriptPath = os.path.dirname(os.path.realpath(__file__))
        rootPath = os.path.dirname(scriptPath)
        pvsConfigDir=os.path.expanduser("~/.config/PVS-Studio")
        pvsLicenseFile=os.path.join(pvsConfigDir, 'PVS-Studio.lic')
        print('Checking PVS Studio credentials...')
        if not os.path.isfile(pvsLicenseFile):
            print(f"PVS License file [{pvsLicenseFile}] does not exist, trying to generate it using supplied command-line parameters")
            if pvs_mail is None:
                raise ValueError('E-mail of the PVS Studio license is not defined. Please use -e parameter')
            if pvs_lic_nr is None:
                raise ValueError('PVS Studio license number is not defined. Please use -n parameter')
            subprocess.run(["mkdir", "-p", pvsConfigDir])
            # Obtain2 license - give it a try here
            subprocess.run(["pvs-studio-analyzer", "credentials", "-o", pvsLicenseFile, pvs_mail, pvs_lic_nr], check=True)
            print(f"...PVS License file [{pvsLicenseFile}] created")
        else:
            print(f"...PVS License file [{pvsLicenseFile}] exists")
        # Create directory for actual log
        outDir = os.path.join(rootPath, "qa", "static_qa_report", dirname)
        print(f'Output directory: {outDir}')
        subprocess.run(["mkdir", "-p", outDir], check=True)
        
        # Filename is the same as directory name
        filename = dirname + ".log"
        sOutputLogFile = os.path.join(outDir, filename)

        passed = True
        if nJobCount is None:
            nJobCount = multiprocessing.cpu_count()

        sTraceFile = os.path.join(rootPath, "strace_out")
        # Run pvs-studio analyzer
        print(f'Executing PVS Studio analyzer ({nJobCount} jobs)...')
        subprocess.run(["pvs-studio-analyzer", "analyze", "-l" , pvsLicenseFile, "-f", sTraceFile, "-o", sOutputLogFile, f"-j{nJobCount}"], check=True)

        # Run html report, compress and copy to artifacts location
        zipped_name = f"{dirname}.tar.gz" 
        print('Executing PVS Studio log converter...')
        outHtmlPath = os.path.join(os.getcwd(), "fullhtml")
        if os.path.exists(outHtmlPath) and os.path.isdir(outHtmlPath):
            shutil.rmtree(outHtmlPath)
        subprocess.run(["plog-converter","-a", "GA:1,2" ,"-t", "fullhtml", sOutputLogFile, "-o", "./"], check=True)
        print(f'HTML report is generated in {os.path.join(os.getcwd(), "fullhtml")}')

        print(f'Compressing output directory {outHtmlPath} to {zipped_name}')
        subprocess.run(["tar","-zcf", zipped_name, "fullhtml"])

        #Copy to artifacts folder
        artifactsDir = os.path.join(scriptPath, "artifacts") 
        subprocess.run(["mkdir", "-p", artifactsDir])
        subprocess.run(["cp", zipped_name, artifactsDir])

        # Send logfile (without full report) if password is given
        if pwd is not None:
            if from_mail is None:
                    from_mail = DEFAULT_EMAIL
            if to_mail is None:
                    to_mail = DEFAULT_EMAIL

            msg = MIMEMultipart()

            message = f"Static code analysis:{filename}"

            msg['From'] = from_mail
            msg['To'] = to_mail
            msg['Subject'] = f"Static code analysis: {filename}"
            password = pwd # SHALL be an input parameter

            part = None
            files = []
            files.append(filename)

            for f in files or []:
                with open(f, "rb") as fil:
                    part = MIMEApplication(
                        fil.read(),
                        Name=os.path.basename(f)
                    )
                # After the file is closed
                part['Content-Disposition'] = 'attachment; filename="%s"' % os.path.basename(f)
                msg.attach(part)

            print(f'Sending code analysis results to e-mail: {to_mail}')
            server = smtplib.SMTP(DEFAULT_SMTP_SERVER, DEFAULT_SMTP_PORT)
            server.starttls()
            server.login(msg['From'], password)
            server.sendmail(msg['From'], msg['To'], msg.as_string())
            server.quit()

    except Exception as bs:
            print (bs)
            passed = False


    if not passed:
        print("!!! One or more test stages failed !!!")
        sys.exit(1)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()

    parser.add_argument('-p', metavar = 'p', help='Pwd for mail server')
    parser.add_argument('-r', metavar = 'r', help='Log receiver mail address')
    parser.add_argument('-s', metavar = 's', help='Log sender mail address (gmail only)')
    parser.add_argument('-j', metavar = 'j', help='Job count')

    parser.add_argument('-e', metavar = 'e', help='Email of PVS-Studio license')
    parser.add_argument('-n', metavar = 'n', help='PVS-Studio license number.')

    args = parser.parse_args()

    main(args.p, args.s, args.r, args.e, args.n, args.j)
