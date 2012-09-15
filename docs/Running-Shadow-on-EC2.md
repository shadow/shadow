Shadow can be run on Amazon’s [Elastic Compute Cloud (EC2)](http://aws.amazon.com/ec2/) infrastructure. This provides a simple and relatively [cost-efficient](http://aws.amazon.com/ec2/#pricing) way to run large-scale Tor experiments in Shadow without the need to buy expensive hardware or manage complex configurations. 

## getting started with our public AMI

Our EC2 AMI can help you get running Tor experiments on EC2 in minutes:

1. Sign up for [Amazon EC2 access](https://aws-portal.amazon.com/gp/aws/developer/registration)
1. Launch an instance using our pre-installed and configured [Shadow-cloud AMI (ami-c178c8a8)](https://console.aws.amazon.com/ec2/home?region=us-east-1#launchAmi=ami-c178c8a8) based on Ubuntu-12.04 LTS
1. Follow the New Instance Wizard
   + the **instance type** you’ll need depends on what size Shadow-Tor network you’ll want to simulate (see [[the plug-in page|Using the scallion plug-in]])
   + create and download a new **keypair** if you don’t already have one, since you’ll need it for SSH access
   + create a new **security group** for the Shadow-cloud server
   + configure the **firewall** to allow inbound SSH on 0.0.0.0/0
1. Once the instance is launched and ready, find the public DNS info and remotely log into the machine using the keypair you downloaded:
```bash
ssh -i your-key.pem ubuntu@your-public-dns.amazonaws.com
```
1. Once logged in, view `~/README` and `~/shadow-git-clone/README` for more information