#--
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#++

module Qpid::Proton

  # A Delivery maintains detail on the delivery of data to an endpoint.
  #
  # A Delivery has a single parent Qpid::Proton::Link
  #
  # @example
  #
  #   # SCENARIO: An event comes in notifying that data has been delivered to
  #   #           the local endpoint. A Delivery object can be used to check
  #   #           the details of the delivery.
  #
  #   delivery = @event.delivery
  #   if delivery.readable? && !delivery.partial?
  #     # decode the incoming message
  #     msg = Qpid::Proton::Message.new
  #     msg.decode(link.receive(delivery.pending))
  #   end
  #
  class Delivery

    include DeliveryState

    # @private
    include Util::Wrapper

    # @private
    def self.wrap(impl) # :nodoc:
      return nil if impl.nil?
      self.fetch_instance(impl, :pn_delivery_attachments) || Delivery.new(impl)
    end

    # @private
    def initialize(impl)
      @impl = impl
      @local = Disposition.new(Cproton.pn_delivery_local(impl), true)
      @remote = Disposition.new(Cproton.pn_delivery_remote(impl), false)
      self.class.store_instance(self, :pn_delivery_attachments)
    end

    # @private
    include Util::SwigHelper

    # @private
    PROTON_METHOD_PREFIX = "pn_delivery"

    # @!attribute [r] tag
    #
    # @return [String] The tag for the delivery.
    #
    proton_caller :tag

    # @!attribute [r] writable?
    #
    # A delivery is considered writable if it is the current delivery on an
    # outgoing link, and the link has positive credit.
    #
    # @return [Boolean] Returns if a delivery is writable.
    #
    proton_caller :writable?

    # @!attribute [r] readable?
    #
    # A delivery is considered readable if it is the current delivery on an
    # incoming link.
    #
    # @return [Boolean] Returns if a delivery is readable.
    #
    proton_caller :readable?
    # @!attribute [r] updated?
    #
    # A delivery is considered updated whenever the peer communicates a new
    # disposition for the dlievery. Once a delivery becomes updated, it will
    # remain so until cleared.
    #
    # @return [Boolean] Returns if a delivery is updated.
    #
    # @see #clear
    #
    proton_caller :updated?

    # @!method clear
    #
    # Clear the updated flag for a delivery.
    #
    proton_caller :clear

    # @!attribute [r] pending
    #
    # @return [Integer] Return the amount of pending message data for the
    # delivery.
    #
    proton_caller :pending

    # @!attribute [r] partial?
    #
    # @return [Boolean] Returns if the delivery has only partial message data.
    #
    proton_caller :partial?

    # @!attribute [r] settled?
    #
    # @return [Boolean] Returns if the delivery is remotely settled.
    #
    proton_caller :settled?

    # Update the state of the delivery
    # @param state [Integer] the delivery state, defined in {DeliveryState}
    def update(state) Cproton.pn_delivery_update(@impl, state); end

    # Settle a delivery, optionally update state before settling
    # A settled delivery can never be used again.
    # @param state [Integer] the delivery state, defined in {DeliveryState}
    def settle(state = nil)
      update(state) unless state.nil?
      Cproton.pn_delivery_settle(@impl)
    end

    # Accept the receiveed message.
    def accept() settle ACCEPTED; end

    # Reject a received message that is considered invalid.
    def reject() settle REJECTED; end

    # FIXME aconway 2017-11-23: why the delivered argument?

    # Release a received message making it available to other receivers.
    def release(delivered = true) settle(delivered ? MODIFIED : RELEASED); end

    # @!method dump
    #
    #  Utility function for printing details of a delivery.
    #
    proton_caller :dump

    # @!attribute [r] buffered?
    #
    # A delivery that is buffered has not yet been written to the wire.
    #
    # Note that returning false does not imply that a delivery was definitely
    # written to the wire. If false is returned, it is not known whether the
    # delivery was actually written to the wire or not.
    #
    # @return [Boolean] Returns if the delivery is buffered.
    #
    proton_caller :buffered?

    # Returns the local disposition state for the delivery.
    #
    # @return [Disposition] The local disposition state.
    #
    def local_state
      Cproton.pn_delivery_local_state(@impl)
    end

    # Returns the remote disposition state for the delivery.
    #
    # @return [Disposition] The remote disposition state.
    #
    def remote_state
      Cproton.pn_delivery_remote_state(@impl)
    end

    # Returns the parent link.
    #
    # @return [Link] The parent link.
    #
    def link
      Link.wrap(Cproton.pn_delivery_link(@impl))
    end

    # Returns the parent session.
    #
    # @return [Session] The session.
    #
    def session
      self.link.session
    end

    # Returns the parent connection.
    #
    # @return [Connection] The connection.
    #
    def connection
      self.session.connection
    end

    # Returns the parent transport.
    #
    # @return [Transport] The transport.
    #
    def transport
      self.connection.transport
    end

    # @private
    def local_received?
      self.local_state == Disposition::RECEIVED
    end

    # @private
    def remote_received?
      self.remote_state == Disposition::RECEIVED
    end

    # @private
    def local_accepted?
      self.local_state == Disposition::ACCEPTED
    end

    # @private
    def remote_accepted?
      self.remote_state == Disposition::ACCEPTED
    end

    # @private
    def local_rejected?
      self.local_state == Disposition::REJECTED
    end

    # @private
    def remote_rejected?
      self.remote_state == Disposition::REJECTED
    end

    # @private
    def local_released?
      self.local_state == Disposition::RELEASED
    end

    # @private
    def remote_released?
      self.remote_state == Disposition::RELEASED
    end

    # @private
    def local_modified?
      self.local_state == Disposition::MODIFIED
    end

    # @private
    def remote_modified?
      self.remote_state == Disposition::MODIFIED
    end

  end

end
