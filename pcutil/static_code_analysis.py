#!/usr/bin/env python3

import subprocess
import sys
import time
import smtplib
from email.mime.multipart import MIMEMultipart
from email.mime.application import MIMEApplication
import os
import argparse


def main(pwd, from_mail , to_mail, pvs_mail,pvs_lic_nr):
    timestr = time.strftime("%Y%m%d_%H%M%S")
    dirname = "static_code_analysis_{}".format(timestr)
    passed = False

    try:
        subprocess.run(["mkdir","-p","~/.config/PVS-Studio"])
        # Obtain license - give it a try here
        subprocess.run(["pvs-studio-analyzer","credentials", pvs_mail ,pvs_lic_nr])

        # Create directory for actual log
        subprocess.run(["mkdir","-p", "/pastel/qa/static_qa_report/{}".format(dirname)])
        
        # Filename is the same as directory name
        filename = dirname + ".log"

        passed = True

        # Run pvs-studio analyzer
        subprocess.run(["pvs-studio-analyzer","analyze", "-l" ,"~/.config/PVS-Studio/PVS-Studio.lic", "-o", filename, "-j2"])

        # Run html report, compress and copy to artifacts location
        zipped_name = "{}.tar.gz".format(dirname) 
        subprocess.run(["plog-converter","-a", "GA:1,2" ,"-t", "fullhtml", filename, "-o", "./"])

        subprocess.run(["tar","-zcf", zipped_name, "fullhtml"])

        #Copy to artifacts folder
        subprocess.run(["mkdir","-p", "/pastel/pcutil/artifacts"])
        subprocess.run(["cp", zipped_name, "/pastel/pcutil/artifacts"])

        # Send lofgile (without full report) if password is given
        if pwd is not None:

            if from_mail is None:
                    from_mail = "pasteltest51@gmail.com"
                    
            if to_mail is None:
                    to_mail= "pasteltest51@gmail.com"

            msg = MIMEMultipart()

            message = "Static code analysis:{}".format(filename)

            msg['From'] = from_mail
            msg['To'] = to_mail
            msg['Subject'] = "Static code analysis: {}".format(filename)
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


            server = smtplib.SMTP('smtp.gmail.com', 587)

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

    parser.add_argument('-e', metavar = 'e', help='Email of PVS-Studio license')
    parser.add_argument('-n', metavar = 'n', help='PVS-Studio license nr.')

    args = parser.parse_args()

    main(args.p,args.s,args.r, args.e, args.n)
