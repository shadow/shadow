# Ensure we have the correct permissions on the hs dir, or Tor won't run
chmod 700 shadow.data.template/hosts/hiddenserver/hs
# Run the Tor minimal test and store output in shadow.log
shadow --template-directory shadow.data.template tor-minimal.yaml > shadow.log
