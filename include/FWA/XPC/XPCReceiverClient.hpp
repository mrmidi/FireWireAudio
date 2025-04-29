//
//  XPCReceiverClient.hpp
//  avc-apple2
//
//  Created by Alexander Shabelnikov on 14.02.2025.
//

#ifndef XPC_RECEIVER_CLIENT_HPP
#define XPC_RECEIVER_CLIENT_HPP

#import <Foundation/Foundation.h>
#import "DuetXPCClientProtocol.h"
#include "Isoch/interfaces/ITransmitPacketProvider.hpp"

@interface XPCReceiverClient : NSObject <DuetXPCClientProtocol>
- (instancetype)init;
@property (nonatomic, assign) FWA::Isoch::ITransmitPacketProvider *processor; 
@end

#endif /* XPC_RECEIVER_CLIENT_HPP */
