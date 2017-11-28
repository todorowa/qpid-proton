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

  # States of a delivery
  module DeliveryState
    # Message was successfully processed by the receiver
    ACCEPTED = Cproton::PN_ACCEPTED

    # Message rejected as invalid and unprocessable by the receiver.
    REJECTED = Cproton::PN_REJECTED

    # Message was not (and will not be) processed by the receiver, but may be
    # acceptable if re-delivered to another receiver
    RELEASED = Cproton::PN_RELEASED

    # Like released, but the disposition includes modifications to be made to
    # the message before re-delivery
    MODIFIED = Cproton::PN_MODIFIED

    # Partial message data was received, message can be resuemed - used only during link recovery.
    RECEIVED =  Cproton::PN_RECEIVED
  end

  # Disposition records the current state and/or final outcome of a transfer.
  #
  # Every delivery contains both a local and a remote disposition. The local
  # disposition holds the local state of the delivery, and the remote
  # disposition holds the *last known* remote state of the delivery.
  #
  class Disposition

    include DeliveryState

    attr_reader :impl

    # @private
    def initialize(impl, local)
      @impl = impl
      @local = local
      @data = nil
      @condition = nil
      @annotations = nil
    end

    # @private
    include Util::SwigHelper

    # @private
    PROTON_METHOD_PREFIX = "pn_disposition"

    # @!attribute section_number
    #
    # @return [Integer] The section number of the disposition.
    #
    proton_accessor :section_number

    # @!attribute section_offset
    #
    #  @return [Integer] The section offset of the disposition.
    #
    proton_accessor :section_offset

    # @!attribute failed?
    #
    # @return [Boolean] The failed flag.
    #
    proton_accessor :failed, :is_or_get => :is

    # @!attribute undeliverable?
    #
    # @return [Boolean] The undeliverable flag.
    #
    proton_accessor :undeliverable, :is_or_get => :is

    # Sets the data for the disposition.
    #
    # @param data [Codec::Data] The data.
    #
    # @raise [AttributeError] If the disposition is remote.
    #
    def data=(data)
      raise AttributeError.new("data attribute is read-only") unless @local
      @data = data
    end

    # Returns the data for the disposition.
    #
    # @return [Codec::Data] The data.
    #
    def data
      if @local
        @data
      else
        Codec::Data.to_object(Cproton.pn_disposition_data(@impl))
      end
    end

    # Sets the annotations for the disposition.
    #
    # @param annotations [Codec::Data] The annotations.
    #
    # @raise [AttributeError] If the disposition is remote.
    #
    def annotations=(annotations)
      raise AttributeError.new("annotations attribute is read-only") unless @local
      @annotations = annotations
    end

    # Returns the annotations for the disposition.
    #
    # @return [Codec::Data] The annotations.
    #
    def annotations
      if @local
        @annotations
      else
        Codec::Data.to_object(Cproton.pn_disposition_annotations(@impl))
      end
    end

    # Sets the condition for the disposition.
    #
    # @param condition [Codec::Data] The condition.
    #
    # @raise [AttributeError] If the disposition is remote.
    #
    def condition=(condition)
      raise AttributeError.new("condition attribute is read-only") unless @local
      @condition = condition
    end

    # Returns the condition of the disposition.
    #
    # @return [Codec::Data] The condition of the disposition.
    #
    def condition
      if @local
        @condition
      else
        Condition.convert(Cproton.pn_disposition_condition(@impl))
      end
    end

  end

end
